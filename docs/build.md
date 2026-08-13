# TexSolve 构建与安装规范

## 1. 支持环境

首版支持 Windows x64、MSYS2 UCRT64、GCC C++20 和 Ninja。参考环境为 GCC 16.1.0、CMake 4.3.2、Ninja 1.13.2、pkg-config 2.5.1，并要求 UCRT64 GetText/libintl。最低 CMake 版本定为 3.25；实现不得依赖高于该版本才有的语义。

用户可见文本由 `LANG`/`LC_ALL` 选择 `en_US.UTF-8` 或 `zh_CN.UTF-8`。GetText 按 `LC_ALL > LC_MESSAGES > LANG` 取值；测试或脚本需要覆盖语言时应同时设置 `LANG` 与 `LC_ALL`。

CLI 与 DLL 自动从相邻安装前缀查找 `share/locale`。外部程序静态链接 `TexSolve::static` 时，应在首次创建 context 前把 `TEXSOLVE_LOCALE_DIR` 设为 `<prefix>/share/locale`；这避免把构建机的绝对安装路径写入静态库。

所有命令从仓库根目录通过 UCRT64 包装执行：

```powershell
bash -c 'cmake -G Ninja -S . -B build_gcc -DCMAKE_BUILD_TYPE=Release'
bash -c 'cmake --build build_gcc --parallel $(nproc)'
bash -c 'ctest --test-dir build_gcc --output-on-failure'
```

## 2. CMake 目标

| 构建目标 | 输出 | 安装后目标 | 说明 |
|---|---|---|---|
| `texsolve_shared` | `texsolve.dll` + import library | `TexSolve::shared` | 导出 C ABI |
| `texsolve_static` | `libtexsolve_static.a` | `TexSolve::static` | 定义 `TEXSOLVE_STATIC` |
| `texsolve_cli` | `texsolve.exe` | 无库别名 | 默认链接 shared |
| `texsolve_tests` | 测试可执行文件集合 | 不安装 | CTest 注册 |
| `texsolve_static_consumer` | 静态 ABI smoke test | 不安装 | 验证同一公开头 |

shared/static 必须复用同一 object library 或同一源码清单，避免实现分叉。公共 include 路径只暴露 `include/`；内部 AST 和适配器头位于 `src/`，不得安装。

## 3. 离线第三方依赖

数学后端只从 `${PROJECT_SOURCE_DIR}/third_party` 搜索；GetText 工具和 libintl 使用 UCRT64 环境的系统包。配置阶段不得使用 FetchContent 或 ExternalProject 下载依赖。各类别映射如下：

根目录 `third_party.info.md` 是依赖库存与早期推荐记录；若其职责措辞与本节或 [design.md](design.md) 冲突，以这两份正式规范为准。特别是 Boost.Math 只承担数值积分备选，ODE 固定使用 SUNDIALS；Ceres/NLopt 承担本项目的一般非线性优化接口。

| 依赖 | 仓库路径 | 用途 |
|---|---|---|
| Boost 1.86 | `third_party/boost` | Spirit X3、Program_options、Boost.Test、Boost.Math |
| SymEngine 0.14 / GiNaC 1.8.9 | 对应目录 | 符号后端 |
| GMP 6.3 / MPFR 4.2 | 对应目录 | 精确整数/有理数与高精度 |
| Eigen 3.4 / Armadillo 12.6.4 | 对应目录 | 线性代数后端 |
| GSL 2.7.1 | `third_party/gsl` | 数值积分 |
| Ceres 2.2 / NLopt 2.10 | 对应目录 | 优化后端 |
| SUNDIALS 7.5 | `third_party/sundials` | ODE IVP |
| OpenBLAS 0.3.25 / CLN 1.3.7 | 对应目录 | Armadillo/GiNaC 传递依赖 |

优先使用各安装树提供的 CMake config；无 config 时创建仓库内 imported target，显式声明 include、archive 和传递库。禁止在业务目标中散落绝对 `.a` 路径。配置必须检查每个头和库文件，缺失时一次列出所有缺项并失败。

## 4. 编译规则

- `CMAKE_CXX_STANDARD=20`、`CMAKE_CXX_STANDARD_REQUIRED=ON`、`CMAKE_CXX_EXTENSIONS=OFF`。
- C ABI 实现不得让异常越过导出函数；内部可以使用异常，但入口统一捕获并映射状态。
- Debug 启用断言；Release 使用现有工具链默认优化，不额外强制 `-march=native`。
- shared/static 使用相同告警集；项目代码至少启用 `-Wall -Wextra -Wpedantic`，第三方头按 SYSTEM include 处理。
- 不把 `BUILD_SHARED_LIBS` 作为选择开关，因为首版必须同时生成两种库。

## 5. 配置选项

只提供实际需要的开关：

| 选项 | 默认值 | 行为 |
|---|---|---|
| `TEXSOLVE_BUILD_TESTS` | `ON`（顶层） | 构建并注册测试 |
| `TEXSOLVE_BUILD_CLI` | `ON` | 构建动态链接 CLI |
| `TEXSOLVE_ENABLE_INSTALL` | `ON` | 生成 install/export 规则 |

不为每个后端提供编译开关；既然首版承诺运行时选择，配置缺少任一承诺后端即失败。

## 6. 安装布局与下游使用

```text
<prefix>/bin/texsolve.exe
<prefix>/bin/texsolve.dll
<prefix>/lib/libtexsolve.dll.a
<prefix>/lib/libtexsolve_static.a
<prefix>/include/texsolve/texsolve.h
<prefix>/share/locale/{en_US,zh_CN}/LC_MESSAGES/texsolve.mo
<prefix>/lib/cmake/TexSolve/TexSolveConfig.cmake
<prefix>/lib/cmake/TexSolve/TexSolveConfigVersion.cmake
<prefix>/lib/cmake/TexSolve/TexSolveTargets.cmake
```

包版本兼容策略为 `SameMajorVersion`。Config 必须验证或定位运行 TexSolve 所需的传递依赖，不得写死构建机绝对路径。

```cmake
find_package(TexSolve 0.1 CONFIG REQUIRED)
target_link_libraries(app PRIVATE TexSolve::shared) # 或 TexSolve::static
```

静态目标通过 INTERFACE compile definition 传播 `TEXSOLVE_STATIC`，并传播其全部静态传递依赖。共享目标只暴露 import library 和公开头。

## 7. 安装验证

```powershell
bash -c 'cmake --install build_gcc --prefix build_gcc/install'
bash -c 'cmake -G Ninja -S tests/consumer -B build_consumer -DCMAKE_PREFIX_PATH="$PWD/build_gcc/install"'
bash -c 'cmake --build build_consumer && ctest --test-dir build_consumer --output-on-failure'
```

consumer 必须分别链接 `TexSolve::shared` 与 `TexSolve::static`。动态 consumer 的测试环境把安装 `bin` 加入 `PATH`，不复制 DLL 到源码树。
