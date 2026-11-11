# PanguMetaTool 宿主 SDK

独立 C++23/Clang 23 工程，SDK 依赖只进入本工程。首版 CLI 已接入：

- `PanguMetaTool --check-sdk`：验证宿主 SDK。
- `PanguMetaTool generate --request <absolute-json>`：读取 CMake 派生的请求，执行 scan/query/catalog 任务并发布生成代码、依赖与成功标记。请求文件路径必须为绝对路径。

两种命令成功返回 0；SDK 检查、请求读取/校验、解析、生成或发布失败返回 1；参数用法错误（含相对请求路径）返回 2。诊断写入 stderr。正常构建通过 `pangu_meta_headers`、`pangu_meta_module`、`pangu_meta_host_catalog` 等登记接入原生图，无需手写请求；接入与支持边界见[构建系统](../../wiki/foundation/build-system.md)和[Reflection / Meta](../../wiki/core/reflection-meta.md)。

## 显式准备

宿主标准库为 Windows/MSVC STL、Linux/macOS/libc++；自有工具和 SDK 都保留 RTTI。
现有 SDK 可先用 `Sdk/Check.cmake` 验证；不兼容时显式调用 `Sdk/Prepare.cmake`。
日常 Engine CMake 不下载源码、不构建 LLVM、不导入 LLVM targets。

调用方先准备独立、长期保留的 `llvm-project` 23.x 源码检出并记录完整 Git commit。
参考提交为 `ea7d852a70e8bdfaf601d6626a760f9771b2c4b4`（23.1.0）。脚本只验证
HEAD 和参数，不执行全树内容审计，不联网、checkout 或安装；不会覆盖现有编译器。来源检出
由调用方显式准备并保持与记录提交一致。输出为构建树开发包，不能单独搬走
源码或 SDK 构建树。跨宿主共享源码检出时，Git 元数据也须能由各宿主读取；建议检出时使用
`core.autocrlf=false`，确保 `llvm/cmake/config.guess` 等 Unix 脚本保持提交中的 LF。

以下变量均由本机显式设置，机器路径只保存在忽略的 BuildRoot 内：

```sh
cmake -DPG_LLVM_SOURCE_DIR=<absolute-source> \
  -DPG_LLVM_SOURCE_COMMIT=<full-commit> \
  -DPG_SDK_BUILD_ROOT=<host-build-root>/Sdk/PanguMeta/LLVM23 \
  -DPG_HOST_COMPILER=<host-clang++> -DPG_BUILD_JOBS=2 \
  -DPG_SDK_CONFIG_FILE=<host-build-root>/Sdk/PanguMeta/PanguMetaSdk.cmake \
  -P BuildTools/PanguMetaTool/Sdk/Prepare.cmake
```

该过程配置 RTTI=ON、EH=OFF、X86/AArch64，Linux/macOS 启用 libc++，只请求
`llvm-libraries`、`clang-libraries`、`clang-resource-headers`，不构建替代目标 clang。
同一 SDK BuildRoot 必须串行；`PG_BUILD_JOBS` 按可用内存设置，链接任务限制为 1。
Linux/macOS 的 `LLVM_ROOT` 必须对应所选宿主 Clang，Linux libc++ 路径沿用仓库 Toolchain。

复用现有 SDK 时：

```sh
cmake -DPANGU_META_LLVM_DIR=<sdk>/lib/cmake/llvm \
  -DPANGU_META_CLANG_DIR=<sdk>/lib/cmake/clang \
  -DPANGU_META_RESOURCE_DIR=<sdk>/lib/clang/23 \
  -DPANGU_META_HOST_COMPILER=<host-clang++> \
  -DPG_SDK_CONFIG_FILE=<host-build-root>/Sdk/PanguMeta/PanguMetaSdk.cmake \
  -P BuildTools/PanguMetaTool/Sdk/Check.cmake
```

资源路径是包含 `include/stddef.h` 的根。预检验证实际标准库、跨 SDK `std::string` ABI、
Clang 基类 RTTI/派生链接，以及使用资源头的原始 C++ TU 解析。只有预检成功才写
`PanguMetaSdk.cmake`，内容仅含 SDK/编译器路径和来源版本；此文件不能提交。

## 独立构建和测试

生产工具位于 `<host-build-root>/BuildTools/PanguMetaTool`；测试工程位于
`<host-build-root>/Tests/BuildTools/PanguMetaTool`，两者不混用缓存。支持原生 Ninja 单配置，
不支持交叉构建宿主工具或多配置生成器。

```sh
cmake -S BuildTools/PanguMetaTool -B <host-build-root>/BuildTools/PanguMetaTool -G Ninja \
  -DPANGU_META_SDK_CONFIG=<sdk-config> -DBUILD_TESTING=OFF
cmake --build <host-build-root>/BuildTools/PanguMetaTool --target PanguMetaTool
cmake -S BuildTools/PanguMetaTool -B <host-build-root>/Tests/BuildTools/PanguMetaTool -G Ninja \
  -DPANGU_META_SDK_CONFIG=<sdk-config> -DBUILD_TESTING=ON
cmake --build <host-build-root>/Tests/BuildTools/PanguMetaTool --target PanguMetaTool PanguMetaSdkTests
ctest --test-dir <host-build-root>/Tests/BuildTools/PanguMetaTool -R '^MetaSdk' --no-tests=error --output-on-failure
```

Windows BuildRoot 为仓库的 `Build/Windows`；Linux 为 `$HOME/PanguEngineBuild/Linux`，
macOS 为 `$HOME/PanguEngineBuild/macOS`。Unix 使用约定的共享源码挂载，不复制 Engine 源码。
Engine 根配置传入 `PANGU_META_SDK_CONFIG` 后，SDK 测试接入现有 BuildTools precommit/full
调度，始终由独立 CMake 子进程加载 SDK。未提供配置时不注册这些需要 SDK 的测试。

`sdk_rtti_required` 表示 RTTI 元数据或真实链接不兼容；`sdk_stdlib_mismatch` 表示宿主
标准库或 SDK 字符串 ABI 不兼容；版本、资源头、TU 解析等失败为 `incompatible_sdk`。
缺 SDK 是前置失败。负例用真实包导出加 RTTI 元数据覆盖验证拒绝路径，标准库负例改变
实际消费者编译参数；正例必须完成真实链接并运行解析，不能由元数据覆盖代替。
