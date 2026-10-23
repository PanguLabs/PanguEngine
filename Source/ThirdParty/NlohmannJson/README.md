# nlohmann/json — Git submodule 接入

第三方依赖来自 [nlohmann/json](https://github.com/nlohmann/json) 官方仓库，
固定使用 **v3.12.0 正式发布版**，不使用 develop 或预览版 API。

上游源码位于 `Repo/` submodule，具体提交以主仓库记录的 gitlink 为准，对应上游
[`v3.12.0`](https://github.com/nlohmann/json/tree/v3.12.0) 标签。
当前固定提交：`55f93686c01528224f448c19128836e7df245f72`。
外层 `CMakeLists.txt` 使用 PanguEngine 的构建上下文创建 header-only Target，
直接引用 `Repo/single_include/nlohmann/`，不配置上游工程或启用其测试。

| 文件 | 用途 |
| --- | --- |
| `json.hpp` | 完整的 JSON DOM、解析、JSON Pointer、字面量与序列化 |
| `json_fwd.hpp` | 仅声明类型，减轻不进行 JSON 运算的头文件依赖 |

许可：MIT，见 `Repo/LICENSE.MIT`。只使用 Header-only + CMake INTERFACE Target
`ThirdParty::NlohmannJson`，不使用 vcpkg、系统包、FetchContent 或构建时下载，
不是 Pangu Module。上游源码不受项目 `.clang-format` 自动改写。
当前没有上游修改，不创建 `Patch/`。

初始化（在 PanguEngine 根目录执行）：

```sh
git submodule update --init --recursive
```

构建配置不执行 Git 更新，也不追踪远程分支。迁移与升级规则见
[ThirdParty 接入规范](../../../wiki/foundation/third-party.md)。

## 使用与优化约束

- 声明层头文件只引用 JSON 类型时使用 `<nlohmann/json_fwd.hpp>`；
  实际解析/访问 JSON 的 `.cpp` 包含 `<nlohmann/json.hpp>`。
  `Tools/AbiProfile/Private/AbiProfile.h` 已这样处理，核心算法放在 `.cpp`。
- CMake 定义 `JSON_USE_GLOBAL_UDLS=0`，禁止把 JSON 字面量操作符引入全局命名空间。
  v3.12.0 的字面量定义由 `<nlohmann/json.hpp>` 提供，仅在需要的局部作用域中
  `using namespace nlohmann::literals;`。
- 项目统一禁用 C++ 异常；工具须使用 `parse(..., allow_exceptions=false)`
  等可失败路径处理非受信任输入。只有静态受控的测试 JSON 字面量/输入才使用
  不返回错误的便利接口。

- `JsonExtras` CTest 验证固定版本、字面量、JSON Pointer、DOM 访问与无异常解析失败路径。
  不引入其他版本的独立头文件或实验性零拷贝 API。

升级必须明确更新版本要求，在 `Repo/` 检出目标提交并更新主仓库 gitlink，复核许可证、
Frozen ABI Canonical JSON/SHA-256 测试向量及 Windows/WSL/macOS Clang 测试。
不得将不同版本或提交的头文件复制混用。
