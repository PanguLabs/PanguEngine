# PanguEngine
A game engine based on c++23, supporting Windows, macOS, Linux, Android, and iOS, and compiled with Clang on all platforms.

架构设计与冻结状态请参阅 [设计文档索引](wiki/README.md)。

构建使用 **CMake 4.0+ / C++23 / Clang / Ninja**，五平台 Presets 及工具链说明见 [构建系统](wiki/foundation/build-system.md)。在对应平台的仓库根目录中，可使用 `cmake --preset windows`、`cmake --preset linux` 或 `cmake --preset macos`，随后执行 `cmake --build --preset <平台名>` 和 `ctest --preset <平台名>`。Android 和 iOS 目前仅验证 Module Identity 静态库交叉编译，不代表应用可运行。
