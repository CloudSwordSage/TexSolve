# TexSolve 系统设计

## 1. 设计目标

TexSolve 采用单进程、分层库结构：一个解析器、一个内部 AST、一个执行调度器，以及按计算类别划分的窄后端适配器。后端不是插件，首版不提供动态注册或第三方扩展点。公开边界只有 [c-api.md](c-api.md) 定义的稳定 C ABI。

## 2. 组件图

```text
CLI / C caller
      |
      v
COMP-1 C ABI facade ---- COMP-2 Context
      |                       |
      v                       v
COMP-3 Boost.Spirit X3 parser -> COMP-4 AST + semantic validator
                                      |
                                      v
                              COMP-5 Operation planner
                               /      |       \
                              v       v        v
                    COMP-6 Symbolic  COMP-7 Numeric  COMP-8 Solver
                              \       |        /
                               v      v       v
                              COMP-9 Result tree
                                      |
                                      v
                             COMP-10 LaTeX renderer
```

| ID | 组件 | 单一职责 | 不负责 |
|---|---|---|---|
| COMP-1 | C ABI facade | 参数校验、异常封装、句柄生命周期 | 数学计算 |
| COMP-2 | Context | 定义、默认精度、限额和后端偏好 | 全局共享状态 |
| COMP-3 | Parser | UTF-8 白名单词法、语法和源区间 | 数学求值 |
| COMP-4 | AST/validator | 不可变 AST、名称解析、维度与操作合法性 | 后端选择 |
| COMP-5 | Operation planner | 判定 operation、解析优先策略和能力匹配 | 实现算法 |
| COMP-6 | Symbolic adapters | SymEngine/GiNaC 转换和符号运算 | 暴露后端对象 |
| COMP-7 | Numeric adapters | MPFR、GSL/Boost.Math、Eigen/Armadillo 数值运算 | 会话管理 |
| COMP-8 | Solver adapters | 方程、Ceres/NLopt 优化、SUNDIALS ODE | 通用插件注册 |
| COMP-9 | Result tree | 后端无关的不可变结构化结果和诊断 | 保存 AST |
| COMP-10 | Renderer | 规范 LaTeX 与近似文本输出 | 重新解析输出 |

## 3. 核心数据模型

内部 AST 使用 C++20 值类型和 `std::variant` 表示，节点持有 `SourceSpan {begin, end}`。节点种类至少包括 literal、symbol、unary、binary、call、definition、derivative、integral、limit、finite_fold、matrix、equation、system、optimization 和 ode_ivp。解析完成后 AST 不可变；变量代入和规范化创建新节点或后端表达式，不原地篡改共享节点。

Context 持有：

- 变量名到规范表达式的映射；
- 用户函数名、形参列表及函数体；
- 默认精度、资源上限和各类别后端偏好；
- 后端所需的 context 私有缓存。

Result tree 只保留公开所需数据：节点 kind、可选名称、精确 LaTeX、可选近似文本、后端名、子节点和诊断。它不引用 AST，也不要求 context 比 result 活得更久。

## 4. 主数据流

1. COMP-1 校验 ABI 版本、结构大小、指针、UTF-8 长度和数值范围。
2. COMP-3 在输入长度和嵌套限制内产生 AST；失败直接产生诊断结果。
3. COMP-4 基于 context 快照解析名称、检查函数参数、矩阵维度和问题完整性。
4. COMP-5 合并数学记法推断与显式 operation；显式 operation 优先，冲突返回 `OPERATION_MISMATCH`。
5. COMP-5 先请求符号路径；仅当需求允许且解析解不可得时规划数值路径。
6. 适配器执行并返回内部结果；阶段边界检查截止时间，后端能力支持时传入剩余预算。
7. COMP-9 将内部结果复制为独立结果树；成功的 definition 最后原子提交到 context。
8. COMP-10 生成规范 LaTeX；CLI 把结果写 stdout，把诊断和调试 AST 写 stderr。

## 5. 后端能力与调度

| 类别 | `AUTO` 规则 | 显式选择 | 共同结果契约 |
|---|---|---|---|
| symbolic | 先 SymEngine；能力缺失时 GiNaC | SymEngine/GiNaC | 规范符号表达式 |
| linear algebra | 符号矩阵先走 symbolic adapter；数值密集矩阵默认 Eigen | Eigen/Armadillo | 矩阵/谱结果树 |
| integration | 可解析先符号；数值定积分默认 GSL | GSL/Boost.Math | 值、误差估计、状态 |
| optimization | residual 数组非空的最小二乘用 Ceres；一般/约束问题用 NLopt，不做表达式形状猜测 | Ceres/NLopt | 终点、目标、收敛信息 |
| precision | 精确整数/有理数 GMP；高精度实数 MPFR | 固定 | 精确或指定精度文本 |
| ODE | SUNDIALS | 固定 | 时间与状态轨迹 |

显式选择后，调度器只验证能力，不尝试备用后端。不同后端在共同能力上的结果结构必须一致；实际后端只通过稳定的 backend accessor 暴露，其他后端特有信息只能放入诊断或后端无关的结果 metadata。

## 6. 解析解与数值退化

- 普通表达式、化简、求导和不定积分只接受解析结果；失败返回 `NO_ANALYTIC_SOLUTION`。
- 定积分、方程、优化和 ODE 可走数值路径，但必须具备边界、初值和停止条件。
- 多项式解析路径返回所有可得根及重数。超越方程数值路径只声明在给定区间或初值附近找到的根。
- 数值结果记录精度、后端、收敛状态和误差信息；不得把近似值写成解析等式。

## 7. 状态、并发与失败原子性

不同 context 无共享可变数学状态，可并发调用。同一 context 保存定义，ABI 明确要求调用方串行。实现不添加全局大锁；第三方库若存在进程级只读初始化，使用 `std::call_once`。

所有请求先读取 context 快照。只有成功的变量/函数定义会在结束时提交；解析失败、后端失败、资源耗尽和超时均不改变 context。Result 拥有自身字符串和子节点，销毁 context 后仍可读取，直至 `texsolve_result_destroy`。

## 8. 资源与安全边界

解析器在创建节点前检查输入、深度和节点数。执行器统一解析零值为 context 默认限额，拒绝超过硬上限的配置。默认值为 64 KiB、128 层、50000 节点、10000 次迭代、5 秒协作式截止和最高 10000 位精度。

截止时间不是线程强杀：在解析、验证、规划、适配转换及后端返回处检查，并传给 NLopt 等支持时限的后端。不可中断第三方调用可能略超时，诊断必须说明实际终止点。

## 9. 需求映射

| 组件 | 需求 |
|---|---|
| COMP-1/COMP-9 | REQ-010、REQ-012、REQ-013 |
| COMP-2 | REQ-003、REQ-009、REQ-013 |
| COMP-3/COMP-4 | REQ-001、REQ-002、REQ-004、REQ-005、REQ-006、REQ-007、REQ-008 |
| COMP-5 | REQ-004、REQ-005、REQ-006、REQ-007、REQ-008、REQ-009 |
| COMP-6/COMP-7/COMP-8 | REQ-002、REQ-004、REQ-005、REQ-006、REQ-007、REQ-008、REQ-009 |
| COMP-10/CLI | REQ-010、REQ-011 |
| CMake/安装 | REQ-012、REQ-014 |
