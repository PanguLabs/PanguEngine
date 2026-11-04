# toml++ — Git submodule 接入

第三方依赖来自 [marzer/tomlplusplus](https://github.com/marzer/tomlplusplus)，固定使用 **v3.4.0 正式 Release**，不跟踪上游主分支。

- 上游 Git Tag：[`v3.4.0`](https://github.com/marzer/tomlplusplus/tree/v3.4.0)。
- 固定 Commit：`30172438cee64926dc41fdd9c11fb3ba5b2ba9de`，由主仓库的 `Repo/` gitlink 记录。
- 许可：MIT，见 `Repo/LICENSE`；没有修改上游代码。
- Include：`Repo/include/toml++/toml.h`；使用 Header-only `ThirdParty::TomlPlusPlus` INTERFACE Target，定义 `TOML_EXCEPTIONS=0`。
- 使用方：独立以宿主 Clang 23/C++23 编译的 `PanguBuildTool`；不作为 Engine Runtime Module，不依赖 `PanguBuildOptions`。

首次检出或切换版本后，在 PanguEngine 根目录执行：

```sh
git submodule update --init --recursive
```

CMake Configure、普通 Ninja Build 和运行 PanguBuildTool 时均**不联网或更新子模块**；缺少头文件则配置失败并给出初始化提示。上游源码不按引擎 `.clang-format` 重排。

源码插件的 `[plugin]` Manifest v1 规范见 [ADR-0014](../../../wiki/decisions/0014-source-plugins-project-build.md)。升级必须明确选择新的上游正式 Release 和 Commit、检查许可证与补丁，并执行 Manifest CTest 与跨宿主构建；不依赖浮动版本或自动更新。