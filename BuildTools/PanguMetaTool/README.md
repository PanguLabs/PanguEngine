# PanguMetaTool

独立 C++23/Clang 23 宿主工具，提供 `generate --request <absolute-json>` 和 `--check-clang`。成功返回 0，Clang 环境/请求/生成失败返回 1，CLI 用法错误返回 2；诊断写入 stderr。

## 正常构建

安装完整 Clang 23 开发包（编译器、LLVM/Clang 头文件和库、CMake 包与资源头），设置 `LLVM_ROOT` 指向安装根，然后初始化仓库固定依赖：

```sh
git submodule update --init --recursive
cmake --preset windows
cmake --build --preset windows
```

Linux/macOS 使用对应 preset。zlib/zstd 由同一个独立工具工程以普通私有静态目标构建，产物留在工具构建目录的 `ThirdParty` 下。不需要独立准备、安装前缀或包管理器；不下载或构建 LLVM，配置不联网、不更新 submodule。Windows 仍需 Visual Studio C++ 工具及所选 LLVM 要求的 DIA SDK，默认从 VS 环境/vswhere 发现，也可用标准 `MSVC_DIA_SDK_DIR` 指定。

Windows 工具使用 MSVC STL；Linux 工具使用 libstdc++；macOS 使用 libc++。编译器别名解析后显式保留 C++ 驱动模式。压缩库匹配 LLVM 导出的 CRT；Clang 适配层匹配 Clang 库 RTTI，其他工具代码与 GoogleTest 保留 RTTI。这些选项只属于独立宿主工具，不进入 Engine 或生成目标，见 [ADR-0019](../../wiki/decisions/0019-meta-tool-normal-build.md)。

配置阶段检查版本、标准库和资源位置；实际 LLVM 链接及属性/AST/constexpr 检查由普通工具构建完成。链接后自检失败会删除失败的工具 exe，避免下次 Ninja 误判为成功；修复环境后重新构建即可。没有 Meta 输入的纯 Query/Catalog 任务仍需要可用工具，纯登记且无任务不会启动工具。

## 独立调试与测试

生产与测试使用不同缓存，各宿主按既有 BuildRoot 隔离：

```sh
cmake -S BuildTools/PanguMetaTool -B <host-build-root>/BuildTools/PanguMetaTool -G Ninja -DBUILD_TESTING=OFF
cmake --build <host-build-root>/BuildTools/PanguMetaTool --target PanguMetaTool
cmake -S BuildTools/PanguMetaTool -B <host-build-root>/Tests/BuildTools/PanguMetaTool -G Ninja -DBUILD_TESTING=ON
cmake --build <host-build-root>/Tests/BuildTools/PanguMetaTool
ctest --test-dir <host-build-root>/Tests/BuildTools/PanguMetaTool --no-tests=error --output-on-failure
```

仅支持原生单配置 Ninja；宿主工具不继承 Engine/移动 Toolchain。父图生成的 `PanguMetaToolBuildInputs.cmake` 仅为原生依赖清单，包含资源目录、所选 LLVM 根和实际输入，不是需要手工准备的 SDK 配置。宿主入口保留源码/编译器/LLVM_ROOT 身份检查；不同身份应使用新缓存，旧准备层自动迁移已退役。

自动发现、缺失安装/开发包/资源、错误标准库、固定源码、压缩往返、构建后失败与重试、正常 no-op，以及原有 Attr/IR/Context/CodeGen/Publish/Cli 均由正式测试覆盖。旧 Prepare 的目录/候选发布测试随废弃流程移除，保留的能力在新入口验证。接线见[构建系统](../../wiki/foundation/build-system.md)，本轮边界见[验收计划](../../docs/superpowers/plans/2026-11-14-pangumeta-normal-build.md)。
