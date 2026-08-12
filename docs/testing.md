# TexSolve 测试与验收规范

## 1. 测试原则

使用 CTest 统一调度，C++ 测试使用仓库已有 Boost.Test，不新增框架。测试必须确定性、离线运行，并从公开行为验证需求。浮点结果使用与请求精度相符的绝对/相对容差；不得对后端私有字符串做断言。

## 2. 测试层次

| 套件 | 主要内容 | 对应需求 |
|---|---|---|
| `parser_tests` | 每条白名单规则的合法/非法黄金用例、AST 源区间、UTF-8 | REQ-001 |
| `core_tests` | 精确数、复数、代入、定义、渲染 | REQ-002、REQ-003、REQ-010 |
| `calculus_tests` | 导数、积分、极限、有限 fold | REQ-004 |
| `linear_tests` | 矩阵运算、维数错误、谱结果 | REQ-005 |
| `equation_tests` | 多项式全部根、解析/数值方程、初值检查 | REQ-006 |
| `optimization_tests` | GENERAL/LEAST_SQUARES/AUTO 分派、Ceres/NLopt 能力、约束、未收敛 | REQ-007、REQ-009 |
| `ode_tests` | 一阶/高阶/系统 IVP、缺初值、DAE 拒绝 | REQ-008 |
| `abi_tests` | 结构大小、版本、空指针、生命周期、异常屏障 | REQ-010、REQ-012 |
| `cli_tests` | 参数/stdin/文件/REPL、退出码、`-debug` stderr | REQ-011 |
| `limit_tests` | 输入、深度、节点、迭代、截止和精度上限 | REQ-013 |
| `build_consumers` | build-tree/install-tree 静态与动态消费 | REQ-012、REQ-014 |

## 3. 关键场景

- 精确优先：`\frac{1}{3}+\frac{1}{6}` 返回 `\frac{1}{2}`，不返回 `0.5` 作为唯一结果。
- 复数：`\sqrt{-1}` 在复数域返回 `i`；根集合顺序稳定。
- 事务定义：已有 `x:=1` 后提交非法 `f(x,x):=x`，context 仍只包含 `x`。
- 解析失败：未知命令和不完整分数返回精确字节区间，AST 不可用。
- 后端覆盖：显式 Ceres 处理一般不等式约束返回 `BACKEND_UNSUPPORTED`，不得转 NLopt。
- 方程：`x^4-1=0` 返回四个根；数值超越方程无初值返回 `INVALID_ARGUMENT`。
- ODE：`\frac{dy}{dt}=y,\;y(0)=1,\;t\in[0,1]` 的终点接近 `e`；缺区间和 DAE 均拒绝。
- 输出通道：成功结果只在 stdout；`-debug` AST 和诊断只在 stderr。
- 生命周期：销毁 context 后 result 的所有 view 仍可读；销毁 result 后不再访问。
- 并发：多个 context 并行重复计算结果一致；不测试同一 context 并发，因为契约禁止。

## 4. 后端一致性

每组可替换后端维护一个共同能力数据集：

- SymEngine/GiNaC：基本化简、求导、可解析多项式。
- Eigen/Armadillo：实/复矩阵四则、逆、秩和特征值。
- GSL/Boost.Math：平滑有限定积分及误差范围。
- Ceres/NLopt：共同的无约束最小二乘问题。

共同数据集比较数学等价或数值容差，不比较表达式字符串。只属于单一后端的能力测试预期另一后端返回 `BACKEND_UNSUPPORTED`。

## 5. C ABI 与 CLI 验收

ABI 测试使用 C 编译单元包含公开头，另用 C++ 编译单元验证 `extern "C"`。必须覆盖较小 `struct_size`、较大结构尾部、旧/新 binding 大小与不同 `binding_stride`、binding/residual 数组乘法溢出、residual 空/非空指针与逐项 UTF-8、聚合输入字节预算、错误 ABI 版本、固定枚举数值、diagnostic 最小尺寸、含 NUL 的 view、空 output 和重复销毁约束。结果测试必须分别遍历数学 child 和 metadata accessor，按名称读取根重数/搜索种类、线代精度、优化终止信息及 ODE 步数/终止原因，并验证 context snapshot 的 variables/functions/config schema。重复销毁非空悬空指针不受支持；销毁 NULL 必须安全。

CLI 退出码固定为：0 成功、2 用法/参数、3 解析或语义、4 不支持/无解析解、5 后端未收敛、6 资源/截止、70 内部错误。文件与 stdin 冲突时返回 2。无输入源且 stdin 为终端时进入 REPL；管道空输入返回 2，不进入 REPL。

## 6. 资源与性能

每项默认限额以及 [c-api.md](c-api.md) 的每项硬上限都必须有“等于边界成功”和“超过边界返回固定 status/code”用例。截止测试使用可控测试后端或阶段检查点，避免依赖机器速度；真实后端另做非阻塞集成测试。

性能基准在 Release、单线程、同一预热 context 上运行：准备一条不超过 1 KiB、含分数/幂/三角函数和变量代入的标量表达式，预热 5 次，再测 100 次。记录 p50/p95，验收 p95 ≤ 50 ms。优化和 ODE 不设固定求解时间，只验证默认 5 秒预算被传递并产生正确终止状态。

## 7. 文档追踪门禁

合并前执行一个只读检查，提取 [requirements.md](requirements.md) 中所有 `REQ-NNN`，并确认：

1. 每个编号出现在 [design.md](design.md) 的组件映射中；
2. 每个编号出现在 [tasks.md](tasks.md) 至少一个任务中；
3. 每个编号出现在本文至少一个测试套件或场景中；
4. 七份文档的相对链接目标存在；
5. 文档不包含任何未决占位符。

## 8. 完成命令

```powershell
bash -c 'cmake -G Ninja -S . -B build_gcc -DCMAKE_BUILD_TYPE=Release'
bash -c 'cmake --build build_gcc --parallel $(nproc)'
bash -c 'ctest --test-dir build_gcc --output-on-failure'
bash -c 'cmake --install build_gcc --prefix build_gcc/install'
```
