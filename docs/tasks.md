# TexSolve 实施任务

所有任务默认未开始。每个任务必须在对应功能分支完成，留下可运行验证；功能新增与重构不得混入同一提交。

## 1. 构建与 ABI 基线

- [x] **TASK-001 建立 CMake 目标与离线依赖导入**
  - 生成 shared、static、CLI 和测试目标，只使用 `third_party/`。
  - 配置时一次报告全部缺失头/库。
  - 验证：Release 配置和空实现链接成功。
  - _需求：REQ-014；组件：CMake/安装_

- [x] **TASK-002 实现公开 C 头与生命周期骨架**
  - 实现 ABI 1 的枚举、结构、context/result 创建销毁和异常屏障。
  - 验证：C/C++ ABI smoke test，静态与动态 consumer 均通过。
  - _需求：REQ-010、REQ-012；组件：COMP-1、COMP-9_

## 2. 解析器与会话

- [x] **TASK-003 实现 Boost.Spirit X3 白名单解析器**
  - 按 EBNF 实现 token、优先级、隐式乘法、源区间和限额。
  - 验证：`parser_tests` 覆盖语法文档每行正反例。
  - _依赖：TASK-002；需求：REQ-001、REQ-013；组件：COMP-3、COMP-4_

- [x] **TASK-004 实现 AST 调试输出与语义验证**
  - 输出确定性缩进 AST；验证名称、函数参数、矩阵维度和问题完整性。
  - 验证：黄金 AST、错误字节区间和 operation 冲突测试。
  - _依赖：TASK-003；需求：REQ-001、REQ-004、REQ-005、REQ-006、REQ-007、REQ-008、REQ-011；组件：COMP-4_

- [x] **TASK-005 实现 context 定义事务**
  - 支持变量/多参数函数定义、列出、清空及请求级快照。
  - 验证：成功定义持久化，失败/超时不修改状态，多 context 并行。
  - _依赖：TASK-003；需求：REQ-003、REQ-013；组件：COMP-2_

## 3. 计算核心与结果

- [x] **TASK-006 实现精确数、复数与结果树**
  - 接入 GMP/MPFR，完成 binding、结构化结果、诊断和规范 LaTeX 渲染。
  - 验证：精确优先、1..10000 位、result 独立生命周期。
  - _依赖：TASK-002、TASK-003；需求：REQ-002、REQ-010、REQ-013；组件：COMP-7、COMP-9、COMP-10_

- [x] **TASK-007 实现符号后端与操作规划**
  - 接入 SymEngine/GiNaC，支持化简、展开、因式分解、代入和解析优先策略。
  - 验证：共同能力等价、显式后端不回退、无解析解状态正确。
  - _依赖：TASK-006；需求：REQ-002、REQ-009；组件：COMP-5、COMP-6_

- [x] **TASK-008 实现微积分**
  - 支持导数、偏导、积分、极限、有限求和/乘积；数值定积分接入 GSL/Boost.Math。
  - 验证：解析结果、数值误差、非法边界和不支持类别。
  - _依赖：TASK-007；需求：REQ-004、REQ-009、REQ-013；组件：COMP-5 至 COMP-7_

## 4. 数学求解器

- [x] **TASK-009 实现线性代数后端**
  - 接入 Eigen/Armadillo，完成矩阵运算、线性方程、秩和特征问题。
  - 验证：共同数据集、符号保留、维数和奇异错误。
  - _依赖：TASK-006；需求：REQ-005、REQ-009；组件：COMP-7、COMP-8_

- [x] **TASK-010 实现方程与方程组求解**
  - 多项式返回全部可得根；一般方程解析优先，数值路径强制初值/区间。
  - 验证：根重数、复根、局部结果标记、缺初值错误。
  - _依赖：TASK-007、TASK-009；需求：REQ-006、REQ-009、REQ-013；组件：COMP-5、COMP-8_

- [x] **TASK-011 实现非线性优化**
  - 接入 Ceres/NLopt，按 optimization kind 与 residual 数组选择一般目标或最小二乘并处理约束，禁止表达式形状猜测。
  - 验证：收敛/未收敛、显式不兼容后端、迭代与截止预算。
  - _依赖：TASK-006；需求：REQ-007、REQ-009、REQ-013；组件：COMP-5、COMP-8_

- [x] **TASK-012 实现 ODE IVP**
  - 接入 SUNDIALS，把高阶 ODE 规范化为一阶系统并返回轨迹。
  - 验证：一阶、高阶、方程组、缺初值以及 DAE/BVP 拒绝。
  - _依赖：TASK-006；需求：REQ-008、REQ-009、REQ-013；组件：COMP-5、COMP-8_

## 5. CLI、安装与整体验收

- [x] **TASK-013 实现动态链接 CLI 与 REPL**
  - 支持参数、stdin、文件、REPL、子命令、后端/精度设置和 `-debug`。
  - 验证：stdout/stderr、退出码、会话定义和所有输入源。
  - _依赖：TASK-005 至 TASK-012；需求：REQ-003、REQ-011、REQ-012；组件：COMP-1、COMP-2、COMP-10_

- [x] **TASK-014 实现安装和 CMake package**
  - 安装头、DLL、静态库、CLI、targets/config/version 文件。
  - 验证：安装树外的 shared/static consumer 完整构建测试。
  - _依赖：TASK-001、TASK-002；需求：REQ-012、REQ-014；组件：CMake/安装_

- [x] **TASK-015 完成资源与性能验收**
  - 覆盖每项上下界、协作式截止、多 context 并行和 100 次性能基准。
  - 验证：全量 CTest 通过且 Release p95 ≤ 50 ms。
  - _依赖：TASK-003 至 TASK-014；需求：REQ-013、REQ-014；组件：全部_

## 6. 追踪矩阵

| 需求 | 设计组件 | 实施任务 | 测试套件 |
|---|---|---|---|
| REQ-001 | COMP-3、COMP-4 | TASK-003、TASK-004 | parser_tests |
| REQ-002 | COMP-6、COMP-7、COMP-10 | TASK-006、TASK-007 | core_tests |
| REQ-003 | COMP-2 | TASK-005、TASK-013 | core_tests、cli_tests |
| REQ-004 | COMP-4 至 COMP-7 | TASK-004、TASK-008 | calculus_tests |
| REQ-005 | COMP-4、COMP-7、COMP-8 | TASK-004、TASK-009 | linear_tests |
| REQ-006 | COMP-4、COMP-5、COMP-8 | TASK-004、TASK-010 | equation_tests |
| REQ-007 | COMP-4、COMP-5、COMP-8 | TASK-004、TASK-011 | optimization_tests |
| REQ-008 | COMP-4、COMP-5、COMP-8 | TASK-004、TASK-012 | ode_tests |
| REQ-009 | COMP-2、COMP-5 至 COMP-8 | TASK-007 至 TASK-012 | backend consistency suites |
| REQ-010 | COMP-1、COMP-9、COMP-10 | TASK-002、TASK-006 | core_tests、abi_tests |
| REQ-011 | COMP-1、COMP-2、COMP-10 | TASK-004、TASK-013 | cli_tests |
| REQ-012 | COMP-1、COMP-9、CMake | TASK-002、TASK-013、TASK-014 | abi_tests、build_consumers |
| REQ-013 | COMP-1 至 COMP-9 | TASK-003、TASK-005、TASK-006、TASK-008、TASK-010 至 TASK-012、TASK-015 | limit_tests、performance |
| REQ-014 | CMake/安装 | TASK-001、TASK-014、TASK-015 | build_consumers、full CTest |
