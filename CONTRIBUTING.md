# Contributing to TexSolve

## 1. 先做这一步

先确认你的改动属于哪一类：

- 新功能：`feature/<描述>`
- 缺陷修复：`fix/<描述>`
- 文档修订：沿用对应功能分支，或单独使用 `docs/<描述>`

不要直接把贡献提交到 `main`。

## 2. 本地环境

当前项目的已知构建环境：

- Windows x64
- MSYS2 UCRT64
- GCC + C++20
- CMake >= 3.25
- Ninja
- UCRT64 的 GetText / libintl

所有 GCC 构建和测试命令都应通过 `bash -c '...'` 执行。

## 3. 拉起项目

```powershell
bash -c 'cmake -G Ninja -S . -B build_gcc -DCMAKE_BUILD_TYPE=Release'
bash -c 'cmake --build build_gcc --parallel $(nproc)'
bash -c 'ctest --test-dir build_gcc --output-on-failure'
```

如果你修改了安装或导出相关逻辑，再补一轮：

```powershell
bash -c 'cmake --install build_gcc --prefix build_gcc/install'
bash -c 'cmake -G Ninja -S tests/consumer -B build_consumer -DCMAKE_PREFIX_PATH="$PWD/build_gcc/install"'
bash -c 'cmake --build build_consumer && ctest --test-dir build_consumer --output-on-failure'
```

## 4. 改动范围约束

请按项目现状改，不要顺手乱扩：

- 公开 ABI 只看 `include/texsolve/texsolve.h`
- 内部实现主要在 `src/`
- 测试在 `tests/`
- 设计与行为约束以 `docs/` 为准

下面这些改动需要同步更新文档：

- 新增/修改 CLI 选项
- 修改 C ABI、状态码、结果结构
- 调整支持平台、构建方式、安装布局
- 变更支持的数学能力或后端选择策略

对应文档通常是：

- `README.md`
- `docs/requirements.md`
- `docs/design.md`
- `docs/build.md`
- `docs/testing.md`
- `docs/c-api.md`

## 5. 代码风格

- 使用 C++20
- 公开头放 `include/`，实现放 `src/`
- 标准库/第三方头用 `<>`，项目头用 `""`
- 不要引入没被明确需要的新依赖
- 优先复用已有 helper、类型和模式
- 修 Bug 时优先修根因，不要只补某一个调用点

如果逻辑不显然，补简短注释，重点说明“为什么”。

## 6. 测试要求

最少做到和改动范围匹配：

- 解析相关：补 `parser_tests.cpp`
- 核心语义或结果树：补 `core_tests.cpp` / `context_tests.cpp`
- 求解器或后端调度：补 `solver_tests.cpp`
- CLI 或 REPL：补 `check_cli.cmake` / `repl_line_editor_tests.cpp`
- 资源限制：补 `limit_tests.cpp`

不要跳过已有失败测试；先修清楚再继续。

## 7. 提交前检查

提交前至少确认这几项：

1. 能完整构建
2. 相关测试通过
3. 文档已同步
4. 没有把无关文件一起带进去
5. 提交信息能单独描述这一件事

推荐提交格式：

```text
feat(scope): add concise description
fix(scope): correct concise description
docs(scope): update concise description
refactor(scope): simplify concise description
test(scope): add concise description
```

## 8. PR 说明建议

PR 描述至少写清楚这 3 件事：

1. 做了什么
2. 为什么要做
3. 怎么验证

可直接套这个模板：

```md
## Summary
- 

## Why
- 

## Test
- [ ] `cmake -G Ninja -S . -B build_gcc -DCMAKE_BUILD_TYPE=Release`
- [ ] `cmake --build build_gcc --parallel $(nproc)`
- [ ] `ctest --test-dir build_gcc --output-on-failure`
```
