# ADR-0019：PanguMetaTool 使用普通工具构建

> Status：Accepted（用户已批准；实施与验收见本轮计划）
> 日期：2026-11-14
> 命名更新：当前自检入口为 --check-clang、ClangInfo/CheckClang，测试组为 MetaClang；下文命令拼写保留决策当时状态，见[命名清理计划](../../docs/superpowers/plans/2026-11-14-pangumeta-clang-naming.md)。
> 替代范围：ADR-0018 的独立 SDK 准备、依赖安装前缀和本机 SDK 配置文件流程；安装版 LLVM、宿主标准库与 RTTI 政策不变。
> 规范：[工具链](../foundation/toolchain.md)、[Reflection / Meta](../core/reflection-meta.md)

## Context

不再自行编译 LLVM 后，保留 Prepare/Check/Inspect 工程与 PanguMetaSdk.cmake 增加了手动接入、缓存迁移和发布配置的复杂度。用户要求把这些职责合并到普通工具构建，并删除源码及 BuildRoot 的独立 SDK 目录。

## Decision

1. 独立 PanguMetaTool 从 LLVM_ROOT 发现 Clang 23 编译器、LLVM/Clang 包与资源头，不消费单独 SDK 配置。配置期检查路径、版本和宿主标准库。
2. 固定 submodule 的 zlib/zstd 为同图普通私有静态目标；仅以标准 Find 模块及命名目标接入 LLVM 包导出，不安装到另一套 SDK 前缀，不使用 vcpkg。
3. Windows 读取 LLVM 包元数据后匹配压缩库 CRT；Clang 类层次匹配 SDK RTTI，其余自有代码和 GoogleTest 保留 RTTI。Linux 工具使用 libstdc++，Engine/生成目标继续使用 libc++。独立工具不继承 Engine Toolchain。
4. 真实 LLVM 链接由普通构建验证。链接后运行 --check-sdk 验证原始属性/AST/constexpr 能力；失败移除本次工具 exe 并使构建失败，后续必须重试。取消配置期链接尚未构建源码依赖的探针。
5. 父 Engine 仍通过独立子进程构建宿主工具，原生输入清单提供资源目录、SDK 输入和压缩库源码；保持正常 no-op，不增加独立 SDK 阶段。

## Consequences

新检出只需安装完整 LLVM 开发包并初始化 submodule，然后走正常 preset。删除 BuildTools/PanguMetaTool/Sdk 及各宿主 BuildRoot/Sdk/PanguMeta。Windows DIA SDK 仍为系统前置安装，可通过 VS 环境或 vswhere 定位。真实 ABI/属性验证失败现在发生于工具构建阶段，路径/版本/标准库错误仍在配置阶段拒绝。

旧 Prepare 专属的目录发布契约废弃，其有效功能迁移为自动发现、错误根/开发包/资源、固定依赖、压缩往返、构建后失败重试及原生增量测试。历史规格/验收记录保留原适用范围，不伪称运行了完整 Engine 或移动矩阵。实际结果见[实施计划](../../docs/superpowers/plans/2026-11-14-pangumeta-normal-build.md)。
