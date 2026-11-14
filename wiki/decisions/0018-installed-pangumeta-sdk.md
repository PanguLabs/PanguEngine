# ADR-0018：PanguMetaTool 复用已安装 LLVM SDK

> SDK 准备层及独立配置流程已由 [ADR-0019](0019-meta-tool-normal-build.md) 替代；其余宿主标准库/RTTI 政策保持。
> Status：Accepted（用户选择整体工具匹配 SDK；实现与验证见本轮计划）
> 日期：2026-11-13
> 更新：2026-11-14，用户要求取消本机手动包管理器步骤；Prepare 通过固定 submodule 原生准备 zlib/zstd，Check 继续只做验证。不改变复用已安装 LLVM 的决策。
> 替代范围：ADR-0017 中宿主工具必须全链保留 RTTI、Linux 宿主工具禁止 libstdc++、允许自建 LLVM SDK 的条款；其余 Meta 架构与目标契约保持有效。
> 规范：[Reflection / Meta](../core/reflection-meta.md)、[工具链](../foundation/toolchain.md)

## Context

Windows 现成 LLVM/Clang 开发包关闭 RTTI；Linux 现成 Clang 开发库使用 libstdc++ 并关闭 RTTI。包内另有 libc++ 不会改变现有开发库的 C++ ABI。为维持原宿主工具政策而自建 LLVM 增加了构建、存储与维护成本。用户明确要求 Windows/Linux 不自行编译 LLVM，并选择整个 PanguMetaTool 匹配现成 SDK。

## Decision

1. PanguMetaTool 独立工程消费已安装的 Clang 23 开发包，不下载、构建或替换 LLVM；缺少 SDK 或配套依赖时明确失败。
2. Windows 宿主工具使用 MSVC STL；Linux 宿主工具及其测试允许使用 libstdc++；macOS 维持 libc++。Linux 例外不进入 Engine、其他 BuildTools 或生成代码。
3. 直接继承或操作 Clang 类层次的编译单元匹配 SDK 的 RTTI 开关，允许局部 `-fno-rtti`；其余自有代码和 GoogleTest 保留 RTTI。SDK 检查如实报告 RTTI，使用真实属性注册、AST 提取与 constexpr 参数求值验证能力，不要求不存在的 SDK RTTI。
4. 目标 Engine 和生成代码继续使用既定平台标准库与 RTTI；宿主前端仍按目标真实编译上下文解析，宿主编译选项不进入目标上下文。
5. 单独前端进程和 libclang C API 不用于本轮改造：前者增加通信、部署成本，后者不能直接覆盖现有自定义属性与 Sema 能力。

## Consequences

不再需要维护项目内 LLVM 构建树。现成 SDK 的 zlib/zstd 配套依赖由固定 submodule 和独立宿主工程原生准备，不需要 vcpkg；共享前端可复用其动态依赖闭包，DIA SDK 等系统工具仍为前置安装。标准库和版本兼容性继续通过真实链接与运行验证，不接受仅改包元数据的伪兼容。旧 SDK 构建目录不自动删除，历史验收保留其原范围。

## Open Questions

长期 SDK 版本升级、发行包获取和跨前端完整语义兼容仍另行设计。本轮实际测试范围见[实施与验收](../../docs/superpowers/plans/2026-11-13-pangumeta-installed-sdk.md)。
