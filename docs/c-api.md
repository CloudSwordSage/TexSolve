# TexSolve C ABI 规范

## 1. 兼容性与头文件

公开头固定为 `include/texsolve/texsolve.h`，可被 C11 和 C++20 编译器包含。项目版本为 `0.1.0`，`TEXSOLVE_ABI_VERSION` 为 `1`。共享库和静态库使用同一头；静态链接方定义 `TEXSOLVE_STATIC`，构建 DLL 时定义 `TEXSOLVE_BUILDING_DLL`。

所有公开函数使用 `TEXSOLVE_API` 和 `TEXSOLVE_CALL`。Windows x64 UCRT64 下调用约定为默认 C 调用约定，符号位于 `extern "C"`。ABI 不公开 STL、异常、第三方类型、`bool` 或编译器相关枚举大小。

```c
typedef struct texsolve_context texsolve_context;
typedef struct texsolve_result texsolve_result;

typedef struct texsolve_string_view {
    const char *data;
    size_t size;
} texsolve_string_view;
```

`texsolve_string_view` 可包含 NUL；`data == NULL` 只在 `size == 0` 时合法。输入 view 仅在调用期间借用。结果访问器返回的 view 在对应 result 销毁前有效。

## 2. 状态与枚举

公开类型均为 `typedef int32_t`，常量用宏或匿名 C enum 定义为下列固定值；不得公开编译器枚举类型。未知值必须返回 `INVALID_ARGUMENT`。

- `texsolve_status`：`OK=0`、`INVALID_ARGUMENT=1`、`ABI_MISMATCH=2`、`INVALID_UTF8=3`、`PARSE_ERROR=4`、`SEMANTIC_ERROR=5`、`OPERATION_MISMATCH=6`、`UNSUPPORTED=7`、`NO_ANALYTIC_SOLUTION=8`、`BACKEND_UNAVAILABLE=9`、`BACKEND_UNSUPPORTED=10`、`NOT_CONVERGED=11`、`RESOURCE_LIMIT=12`、`DEADLINE_EXCEEDED=13`、`INTERNAL_ERROR=14`。
- `texsolve_operation`：`AUTO=0`、`EVALUATE=1`、`SIMPLIFY=2`、`EXPAND=3`、`FACTOR=4`、`DIFFERENTIATE=5`、`INTEGRATE=6`、`LIMIT=7`、`SUM=8`、`PRODUCT=9`、`SOLVE=10`、`LINEAR_ALGEBRA=11`、`OPTIMIZE=12`、`ODE_IVP=13`、`DEFINE=14`。
- `texsolve_result_kind`：`NONE=0`、`INTEGER=1`、`RATIONAL=2`、`REAL=3`、`COMPLEX=4`、`SYMBOLIC=5`、`BOOLEAN=6`、`LIST=7`、`MAPPING=8`、`MATRIX=9`、`ROOT_SET=10`、`ROOT=11`、`OPTIMUM=12`、`TRAJECTORY=13`、`SAMPLE=14`、`METADATA=15`。
- `texsolve_severity`：`INFO=0`、`WARNING=1`、`ERROR=2`。
- 后端：每类 `AUTO=0`；symbolic 的 `SYMENGINE=1`、`GINAC=2`；linear algebra 的 `EIGEN=1`、`ARMADILLO=2`；integration 的 `GSL=1`、`BOOST_MATH=2`；optimization 的 `CERES=1`、`NLOPT=2`。
- `texsolve_optimization_kind`：`AUTO=0`、`GENERAL=1`、`LEAST_SQUARES=2`。AUTO 仅依据 residual 数组是否非空选择，不分析表达式形状。

ABI 1 的 `texsolve_diagnostic_code` 固定为：`NONE=0`、`INVALID_UTF8=1`、`UNKNOWN_COMMAND=2`、`UNEXPECTED_TOKEN=3`、`TRAILING_INPUT=4`、`INVALID_SPAN=5`、`UNKNOWN_NAME=6`、`DUPLICATE_NAME=7`、`ARITY_MISMATCH=8`、`DOMAIN_ERROR=9`、`DIMENSION_MISMATCH=10`、`SINGULAR_MATRIX=11`、`INCOMPLETE_PROBLEM=12`、`OPERATION_CONFLICT=13`、`INPUT_LIMIT=14`、`NESTING_LIMIT=15`、`AST_NODE_LIMIT=16`、`ITERATION_LIMIT=17`、`PRECISION_LIMIT=18`、`DEADLINE=19`、`BACKEND_MISSING=20`、`BACKEND_CAPABILITY=21`、`NOT_CONVERGED=22`、`NUMERICAL_FAILURE=23`、`INTERNAL_EXCEPTION=24`。`25..999` 保留给后续 ABI 1 头文件版本；调用方必须容忍未知 code，并按 severity/message 显示。

## 3. 请求结构

```c
typedef struct texsolve_binding {
    uint32_t struct_size;
    texsolve_string_view name;
    texsolve_string_view value_latex;
    texsolve_string_view lower_latex;
    texsolve_string_view upper_latex;
} texsolve_binding;

typedef struct texsolve_request {
    uint32_t struct_size;
    uint32_t abi_version;
    int32_t operation;
    uint32_t precision_digits;
    uint32_t max_iterations;
    uint32_t deadline_ms;
    uint32_t max_input_bytes;
    uint32_t max_nesting_depth;
    uint32_t max_ast_nodes;
    int32_t symbolic_backend;
    int32_t linear_algebra_backend;
    int32_t integration_backend;
    int32_t optimization_backend;
    int32_t optimization_kind;
    texsolve_string_view latex;
    const texsolve_binding *bindings;
    size_t binding_count;
    size_t binding_stride;
    const texsolve_string_view *residuals;
    size_t residual_count;
} texsolve_request;

typedef struct texsolve_context_options {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t precision_digits;
    uint32_t max_iterations;
    uint32_t deadline_ms;
    uint32_t max_input_bytes;
    uint32_t max_nesting_depth;
    uint32_t max_ast_nodes;
    int32_t symbolic_backend;
    int32_t linear_algebra_backend;
    int32_t integration_backend;
    int32_t optimization_backend;
} texsolve_context_options;
```

调用方必须先零初始化结构，再填写 `struct_size = sizeof(...)` 和 ABI 版本。实现只读取 `struct_size` 覆盖的字段；小于首版必需字段末尾返回 `ABI_MISMATCH`，更大的结构忽略未知尾部。请求中的限额和后端字段为零时使用 context 默认值。binding 名必须唯一，值和边界均为 LaTeX 表达式。求根、优化所需初值通过 `value_latex` 提供，区间/边界通过 lower/upper 提供。context options 的零值表示采用库默认值，而非无限；configure 成功后整体替换 context 默认配置。

| 字段 | 默认值 | 非零合法范围（含端点） | 超界行为 |
|---|---:|---:|---|
| `precision_digits` | 15 | 1..10000 | `RESOURCE_LIMIT` + `PRECISION_LIMIT` |
| `max_iterations` | 10000 | 1..10000000 | `RESOURCE_LIMIT` + `ITERATION_LIMIT` |
| `deadline_ms` | 5000 | 1..3600000 | `RESOURCE_LIMIT` + `DEADLINE` |
| `max_input_bytes` | 65536 | 1..16777216 | `RESOURCE_LIMIT` + `INPUT_LIMIT` |
| `max_nesting_depth` | 128 | 1..4096 | `RESOURCE_LIMIT` + `NESTING_LIMIT` |
| `max_ast_nodes` | 50000 | 1..5000000 | `RESOURCE_LIMIT` + `AST_NODE_LIMIT` |

configure/request 中超过硬上限的非零值立即失败。合法配置生效后，实际资源消耗小于或等于请求上限均可成功，首次尝试超过时返回 `RESOURCE_LIMIT` 与对应 diagnostic code；截止检查时 `elapsed_ms >= deadline_ms` 返回 `DEADLINE_EXCEEDED` + `DEADLINE`。值 0 只表示继承，不接受无限模式。

`binding_count == 0` 时 `bindings` 可为 NULL 且 `binding_stride` 可为 0。否则 `bindings` 必须非空，`binding_stride >= TEXSOLVE_BINDING_V1_SIZE`，并且每个元素地址按 `(const unsigned char *)bindings + index * binding_stride` 计算；实现只读取 `min(element.struct_size, TEXSOLVE_BINDING_V1_SIZE)` 范围。调用方必须令每个元素的 `struct_size >= TEXSOLVE_BINDING_V1_SIZE`。这使新增 binding 尾字段时新旧调用方的数组寻址保持稳定。`texsolve_binding` 与 `texsolve_diagnostic` 是 ABI 1 嵌套结构，不单独携带 `abi_version`；其布局版本由顶层 request/result ABI 决定。

`max_input_bytes` 统计一个 request 中全部 UTF-8 view 的字节总和：顶层 `latex`、每个 binding 的 `name`/`value_latex`/`lower_latex`/`upper_latex` 以及每个 residual。实现必须用溢出安全加法累加，累计值大于上限时返回 `RESOURCE_LIMIT` + `INPUT_LIMIT`。在解引用数组前还必须验证 `binding_count * binding_stride` 和 `residual_count * sizeof(texsolve_string_view)` 不超过 `SIZE_MAX`；乘法溢出返回 `INVALID_ARGUMENT`，且不得访问数组。

一般优化使用 `latex` 描述单一目标，必须令 `optimization_kind=GENERAL` 且 `residual_count=0`。最小二乘必须令 `optimization_kind=LEAST_SQUARES` 并通过 `residuals` 提供至少一个 UTF-8 LaTeX 残差；目标固定为残差平方和，`latex` 仅承载可选约束 relation set，空 view 表示无约束。`AUTO` 在 residual 非空时选择 LEAST_SQUARES，否则选择 GENERAL，禁止通过识别“平方和外形”猜测。`residual_count==0` 时 residuals 可为 NULL，否则必须非空；每个 residual view 同样遵守 `data==NULL` 当且仅当 `size==0`，并逐项执行 UTF-8 与输入长度校验。`texsolve_string_view` 是 ABI 1 固定值类型，数组步长永久固定为 `sizeof(texsolve_string_view)`。

## 4. 公开函数

| 函数 | 参数 | 返回/生命周期 | 错误 |
|---|---|---|---|
| `uint32_t texsolve_abi_version(void)` | 无 | 返回库 ABI 版本，无资源 | 无 |
| `texsolve_status texsolve_context_create(texsolve_context **out)` | 非空输出指针 | 成功写入新 context；由 destroy 释放 | 参数、内存或内部错误 |
| `void texsolve_context_destroy(texsolve_context *ctx)` | 可空 context | 释放 context；空指针无操作 | 不返回错误；不得与执行并发 |
| `texsolve_status texsolve_context_configure(texsolve_context *ctx, const texsolve_context_options *options)` | 非空 context/options | 原子替换默认精度、限额和后端偏好 | 参数、ABI、范围或内部错误；失败不改变配置 |
| `texsolve_status texsolve_context_snapshot(const texsolve_context *ctx, texsolve_result **out)` | 非空 context/out | 返回变量、函数和有效配置的只读结果树；由 result_destroy 释放 | 参数、内存或内部错误 |
| `texsolve_status texsolve_context_reset(texsolve_context *ctx)` | 非空 context | 原子清空变量和函数，保留当前配置 | 参数或内部错误；失败不改变状态 |
| `texsolve_status texsolve_execute(texsolve_context *ctx, const texsolve_request *request, texsolve_result **out)` | 非空 context/request/out | 成功或数学失败均可产生 result；由 result_destroy 释放 | ABI、参数、解析、语义、后端、限额或内部错误 |
| `void texsolve_result_destroy(texsolve_result *result)` | 可空 result | 释放整棵结果树和诊断；空指针无操作 | 不返回错误 |
| `texsolve_status texsolve_result_status(const texsolve_result *result)` | 非空 result | 返回请求最终状态 | 空指针返回 `INVALID_ARGUMENT` |
| `int32_t texsolve_result_kind(const texsolve_result *result)` | 非空 result | 返回稳定 kind；错误结果可为 `NONE` | 空指针返回 `NONE` |
| `texsolve_string_view texsolve_result_name(const texsolve_result *result)` | 非空 result | 借用可选名称，随 result 失效 | 空指针返回空 view |
| `texsolve_string_view texsolve_result_exact_latex(const texsolve_result *result)` | 非空 result | 借用规范解析 LaTeX；不可得时为空 | 空指针返回空 view |
| `texsolve_string_view texsolve_result_approximation(const texsolve_result *result)` | 非空 result | 借用近似文本；未计算时为空 | 空指针返回空 view |
| `texsolve_string_view texsolve_result_backend(const texsolve_result *result)` | 非空 result | 借用实际后端名；纯解析节点可为空 | 空指针返回空 view |
| `size_t texsolve_result_child_count(const texsolve_result *result)` | 非空 result | 返回直接子节点数量 | 空指针返回 0 |
| `const texsolve_result *texsolve_result_child(const texsolve_result *result, size_t index)` | result 与有效下标 | 返回借用子节点，父 result 销毁后失效 | 越界或空指针返回 NULL |
| `size_t texsolve_result_diagnostic_count(const texsolve_result *result)` | 非空 result | 返回诊断数量 | 空指针返回 0 |
| `texsolve_status texsolve_result_diagnostic(const texsolve_result *result, size_t index, texsolve_diagnostic *out)` | 有效 result/index/out | 复制固定布局诊断；其中 message view 借用 result | 参数或 ABI 错误 |
| `const texsolve_result *texsolve_result_metadata(const texsolve_result *result)` | 非空 result | 返回借用的 `METADATA` 节点；无 metadata 时返回 NULL；不计入 child_count | 空指针返回 NULL |

`texsolve_execute` 的函数返回值与 `texsolve_result_status(*out)` 相同。只要实现能构造诊断，数学失败也写入非空 result；只有参数错误或分配失败允许 `*out == NULL`。函数入口先把 `*out` 设为 NULL。

## 5. 诊断结构

```c
typedef struct texsolve_diagnostic {
    uint32_t struct_size;
    int32_t severity;       /* INFO, WARNING, ERROR */
    int32_t code;           /* stable diagnostic code */
    size_t begin_byte;      /* inclusive */
    size_t end_byte;        /* exclusive */
    texsolve_string_view message;
} texsolve_diagnostic;
```

`TEXSOLVE_DIAGNOSTIC_V1_MIN_SIZE` 固定为 `offsetof(texsolve_diagnostic, message) + sizeof(texsolve_string_view)`。调用 `texsolve_result_diagnostic` 前，调用方必须设置 `out->struct_size`；小于该最小值返回 `ABI_MISMATCH` 且不改写除 `struct_size` 外的字段，否则函数只写入该大小与 ABI 1 已知大小的较小者。诊断区间使用原始 UTF-8 字节偏移，满足 `begin_byte <= end_byte <= request.latex.size`。无特定位置的运行期诊断使用 `[0,0)`。消息用于人读，不作为程序分支依据；调用方使用 status 和 code。

## 6. Result tree 约定

- 标量节点的 `exact_latex` 保存解析值；近似存在时 `approximation` 保存十进制文本。
- `COMPLEX` 有且仅有名为 `real`、`imag` 的两个标量子节点。
- `LIST` 的 child 只含数学数据并按稳定数学顺序保存。
- `MAPPING` 的每个子节点以变量名命名。
- `MATRIX` 的 child 只含行 `LIST`，行内 child 只含元素。
- `ROOT_SET` 的 child 只含 `ROOT`，先实根后复根，再按实部/虚部排序。每个 `ROOT` 必须含 `value`（任意标量 kind）、`multiplicity`（`INTEGER`）、`search_kind`（`SYMBOLIC`）三个具名 child；`search_kind` 的精确文本只允许 `analytic`、`interval`、`local`。
- 数值积分结果为名为 `value` 的标量节点；误差与精度通过 metadata 读取。
- `OPTIMUM` 的 child 固定为 `variables`（`MAPPING`）和 `objective`（标量）；迭代、收敛和终止信息只放 metadata。
- `TRAJECTORY` 的 child 只含 `SAMPLE`；每个 sample 的 child 固定为 `t` 与按状态声明顺序排列的具名标量。
- 每次公开 operation 返回的顶层数值结果必须由 `texsolve_result_metadata` 返回唯一 `METADATA`；只有确实使用不同后端或精度独立计算的子结果才有自己的 metadata。普通矩阵元素、复数分量、ROOT.value 和轨迹 sample 继承最近祖先 metadata，不重复存储。metadata 不计入 `texsolve_result_child_count`，其中必含 `precision_digits`（`INTEGER`）；存在可靠估计时含 `error_estimate`（`REAL`），否则省略。实际后端只通过 `texsolve_result_backend` 获取；子节点继承最近祖先的非空 backend accessor 值。`ROOT` metadata 另可含误差；`OPTIMUM` 另含 `iterations`（`INTEGER`）、`converged`（`BOOLEAN`）、`termination_reason`（`SYMBOLIC`）；`TRAJECTORY` 另含 `steps`（`INTEGER`）和 `termination_reason`（`SYMBOLIC`）。
- `termination_reason` 的精确文本只允许 `converged`、`max_iterations`、`deadline`、`resource_limit`、`numerical_failure`、`user_stop`、`backend_failure`；无法精确映射的后端状态统一为 `backend_failure`，原始后端信息仅写诊断 message。
- 所有数值 metadata 的整数使用 `INTEGER`，布尔使用 `BOOLEAN`，状态词使用 `SYMBOLIC` 且 exact text 取本节固定词。新 metadata 只能追加具名子节点；调用方必须按 name 查找而非依赖位置。

### 6.1 Context snapshot schema

`texsolve_context_snapshot` 返回顶层 `MAPPING`，其 child 固定为 `variables`、`functions`、`config`：

- `variables` 是 `MAPPING`；每个具名 child 的 name 是变量名，值为定义的规范表达式节点。
- `functions` 是 `MAPPING`；每个具名函数 child 是 `MAPPING`，含 `parameters`（`LIST`，child 按声明顺序为 `SYMBOLIC`，其 `exact_latex` 是形参名）和 `body`（规范表达式）两个 child。
- `config` 是 `MAPPING`，固定包含 `precision_digits`、`max_iterations`、`deadline_ms`、`max_input_bytes`、`max_nesting_depth`、`max_ast_nodes` 六个 `INTEGER`，以及 `symbolic_backend`、`linear_algebra_backend`、`integration_backend`、`optimization_backend` 四个 `SYMBOLIC`。后端文本固定使用第 2 节常量的小写名称。

三段以及段内条目均按上述顺序返回；调用方仍应按 name 读取。snapshot 不包含后端缓存。

## 7. Context 与线程规则

不同 context 可由不同线程同时调用。同一 context 的 configure、snapshot、execute、reset 和 destroy 必须由调用方串行化。Result 是只读对象，不同 result 可并发读取；同一 result 的访问器也可并发读取。context 销毁不使已返回 result 失效。

定义请求仅在完整成功后提交。任何错误、截止时间或限额失败都不得修改已有定义。库不得保存 request 输入指针。

## 8. CLI 对 ABI 的使用

CLI 的计算、定义、配置和定义列表只调用本规范函数；默认链接共享目标。`-debug` 由 CLI 在调用 ABI 前访问内部解析调试入口完成，该入口不安装、不导出且不属于 ABI。静态消费测试使用同一头并定义 `TEXSOLVE_STATIC`。
