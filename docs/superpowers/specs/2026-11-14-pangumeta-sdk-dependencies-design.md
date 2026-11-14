# PanguMetaTool SDK 配套依赖准备

> Superseded（独立 SDK 准备与配置部分）：现行 [普通工具构建设计](2026-11-14-pangumeta-normal-build-design.md) 已取消该流程；保留原规格与历史验收边界。
状态：实现、独立评审与三桌面独立工具验收完成；基线为 `528c736`。

## 目标与边界

不依赖 vcpkg，不编译 LLVM。zlib/zstd 按仓库 ThirdParty 规范使用固定提交的 submodule；源码由显式 `git submodule update --init --recursive` 获取，CMake 不联网、不更新子模块或修改上游源码。Windows/Linux/macOS 共用源码，依赖构建与安装均位于各宿主本地 BuildRoot。

## 已选方案

将 zlib 1.3.2、zstd 1.5.7 纳入 `Source/ThirdParty`，用独立宿主 CMake 工程构建静态库并导出 SDK 所需查找配置。`Prepare.cmake` 先构建配套依赖，再调用 `Check.cmake`；后者只验证既有环境。LLVM、DIA SDK、编译器、CMake 和 Ninja 为前置安装，不由脚本下载。优先显式匹配现成 SDK 的编译器、架构和 Release CRT。

相比手动安装，固定源码与脚本可在新电脑复现；相比包管理器，不增加另一套获取/构建工具。相比把依赖放入 Engine 主图，独立工程不传播宿主配置到 Runtime 或移动产物。依赖工程可以直接验证压缩/解压与安装后的标准 CMake 导出。

## 接口与安全

保留已有 `PANGU_META_LLVM_DIR`、`PANGU_META_CLANG_DIR`、`PANGU_META_RESOURCE_DIR`、`PANGU_META_HOST_COMPILER`、`PG_SDK_CONFIG_FILE`。Prepare 从配置文件所在目录派生自己专属的依赖构建/安装目录，提供正整数 `PG_BUILD_JOBS` 控制并行。可显式指定 DIA SDK 路径；Windows 默认从 Visual Studio 安装定位。Windows 使用无构建目标的 Inspect 工程加载 SDK 元数据，读取实际 Release CRT；特殊分发可显式指定 `PG_SDK_RUNTIME_LIBRARY`。

构建根通过身份标记防止复用未知或其他来源目录，使用锁避免同根并发准备。上游固定提交、必要源码、宿主编译器与路径在构建前校验。保留拒绝旧 LLVM 源码构建参数的行为，不覆盖原 LLVM 安装、无关依赖目录或其他宿主缓存。Check 消费候选依赖与候选 SDK 配置，成功后才发布正式文件；失败保留旧主配置和其引用配置。原生提示显式覆盖旧 zstd 包缓存。增量重复执行复用 Ninja 结果，配置文件内容不变时保持时间戳。

## 验收

正式测试覆盖固定源码/缺失源码诊断、错误构建目录拒绝、真实 zlib/zstd 压缩往返、安装后的 SDK 依赖发现、Prepare→Check→工具构建、无变化复用及现有属性/生成消费回归。Windows 使用全新原生依赖目录验证不引用 vcpkg；Linux/macOS 验证独立依赖工程及实际 SDK 接入。原有目录保护测试保留。
