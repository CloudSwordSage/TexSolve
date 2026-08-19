# TexSolve

TexSolve 是一个面向 Windows x64 的 LaTeX 数学表达式计算引擎，提供稳定的 C ABI、静态/动态库，以及命令行工具 `texsolve`。

## 项目状态

- 当前版本：`0.1.0`
- 当前 ABI：`1`
- 当前支持环境：Windows x64 + MSYS2 UCRT64 + GCC C++20 + Ninja
- 当前交付形态：`texsolve.dll` / `libtexsolve_static.a` / `texsolve.exe`

## 功能演示

### CLI 演示

```text
$> texsolve simplify '\frac{1}{3}+\frac{1}{6}'
\frac{1}{2} ~= 0.5
$> texsolve solve 'x^2-1=0'
根集合
  根
    value: -1
    multiplicity: 1
    search_kind: analytic
  根
    value: 1
    multiplicity: 1
    search_kind: analytic
$> texsolve --debug differentiate '\sin(x)^2'
[4,5) 函数需要花括号参数
```

### REPL 演示

```text
$> texsolve --repl
> f(x, y) := x^2 + 5y - 5  # 定义函数 f(x, y)
f: f(x, y) := x^2 + 5y - 5
> f(3, 6)
34
> a := 10  # 定义变量 a
a: a := 10
> f(3, 6) + a
44
> \sqrt{f(5, 6)}
\sqrt{50} ~= 7.07106781186548
> x^2 + 5x - 6 = 0
根集合
  根
    value: -6
    multiplicity: 1
    search_kind: analytic
  根
    value: 1
    multiplicity: 1
    search_kind: analytic
> \sqrt{2}
\sqrt{2} ~= 1.4142135623731
> :precision 30  # 设置小数精度为 30 位
> \sqrt{2}
\sqrt{2} ~= 1.41421356237309504880168872421
> :precision 50  # 设置小数精度为 50 位
> \sqrt{2}
\sqrt{2} ~= 1.4142135623730950488016887242096980785696718753769
> :quit
$>
```

## 它能做什么

- 解析 UTF-8 LaTeX 数学表达式
- 优先返回解析结果，必要时返回数值近似
- 通过 `context` 保存变量、函数、默认精度与后端偏好
- 提供稳定的 C ABI，供外部程序链接 shared/static 库
- 提供 CLI 和 REPL，便于脚本调用和交互式计算

当前设计范围覆盖：

- 基础表达式、化简、展开、因式分解
- 导数、积分、极限、有限求和与乘积
- 矩阵与线性代数
- 方程/方程组求解
- 非线性优化
- ODE 初值问题

详细约束见 [docs/requirements.md](docs/requirements.md)。

## 快速开始

> 环境说明: Windows x64 + MSYS2 UCRT64 + GCC C++20 + Ninja

### 1. 第三方库编译

```bash
./scripts/fetch_all.sh
./scripts/windows_build.sh
```

### 2. 配置

```bash
cmake -G Ninja -S . -B build_gcc -DCMAKE_BUILD_TYPE=Release
```

### 3. 编译

```bash
cmake --build build_gcc --parallel $(nproc)
```

### 4. 运行测试

```bash
ctest --test-dir build_gcc --output-on-failure
```

### 5. 安装到本地前缀

```bash
cmake --install build_gcc --prefix build_gcc/install
```

## CLI 用法

当前 `texsolve --help` 输出对应的调用方式：

```text
texsolve [选项] [操作] [表达式]
texsolve [选项] --file <路径>
texsolve --repl
```

常用示例：

```powershell
texsolve simplify '\frac{1}{3}+\frac{1}{6}'
texsolve solve 'x^4-1=0'
texsolve -p 50 integrate '\int_{0}^{1} \frac{1}{1+x^2}\,dx'
texsolve -b symbolic symengine factor 'x^2-1'
texsolve --repl
```

常用选项：

- `-h`, `--help`
- `-v`, `--version`
- `-r`, `--repl`
- `-f`, `--file`
- `-d`, `--debug`
- `-p`, `--precision`
- `-b`, `--backend`

## 库集成

安装后可通过 CMake package 使用：

```cmake
find_package(TexSolve 0.1 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE TexSolve::shared)
```

如需静态链接：

```cmake
find_package(TexSolve 0.1 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE TexSolve::static)
```

公开头文件：

```text
include/texsolve/texsolve.h
```

更完整的 ABI 约定见 [docs/c-api.md](docs/c-api.md)。

## 仓库结构

```text
include/      公开头文件
src/          核心实现与 CLI
tests/        CTest 与 smoke tests
cmake/        CMake 辅助模块与 package 配置
docs/         需求、设计、构建、测试文档
po/           本地化翻译文件
third_party/  离线第三方依赖
```

## 文档索引

- [docs/requirements.md](docs/requirements.md)：需求范围与验收标准
- [docs/design.md](docs/design.md)：系统设计与分层
- [docs/build.md](docs/build.md)：构建、安装与消费方式
- [docs/testing.md](docs/testing.md)：测试策略与验收
- [docs/c-api.md](docs/c-api.md)：稳定 C ABI 说明
- [docs/latex-syntax.md](docs/latex-syntax.md)：LaTeX 白名单语法
- [docs/tasks.md](docs/tasks.md)：任务拆分与追踪

## 已知边界

- 首版只保证 Windows x64、MSYS2 UCRT64、GCC、Ninja
- 不支持完整 TeX 宏系统和文档级 LaTeX
- 不承诺枚举任意超越方程的全部根
- 不支持 DAE、ODE 边值问题和运行时插件扩展

## 贡献

贡献流程见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 许可证

本项目使用 [Apache License 2.0](LICENSE) 许可证。
