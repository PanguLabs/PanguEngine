# PanguEngine
A game engine based on c++23, supporting Windows, macOS, Linux, Android, and iOS, and compiled with Clang on all platforms.

架构设计与冻结状态请参阅 [设计文档索引](wiki/README.md)。

第三方源码使用 Git submodule。首次检出使用 `git clone --recurse-submodules`；已有工作区在构建前执行：

```sh
git submodule update --init --recursive
```

接入目录、项目构建配置与补丁规则见 [ThirdParty 接入规范](wiki/foundation/third-party.md)。

构建使用 **CMake 4.0+ / C++23 / Clang / Ninja**，flecs 运行库以目标 Clang 编译 C99 源码。在对应平台的仓库根目录使用 `cmake --preset windows`、`cmake --preset linux` 或 `cmake --preset macos`，随后执行 `cmake --build --preset <平台名>` 和 `ctest --preset <平台名>`。五平台预设和工具链见[构建系统](wiki/foundation/build-system.md)。

ECS 首版通过 Windows、WSL Linux 和 macOS 的双链接模式运行验收，以及 Android/iOS ARM64 的双链接模式最终链接和宿主产物检查。移动设备运行、打包和签名待验收。[ECS 集成](wiki/runtime/ecs.md) · [最终验收](docs/superpowers/plans/2026-11-18-ecs-world.md#ecs-交付验收)
