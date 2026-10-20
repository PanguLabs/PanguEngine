# ADR-0001：采用 CMake 作为构建系统基础

> Status: Accepted
> 决策背景：2026-10-12 的构建系统讨论
> 归档日期：2026-10-19

## Context

PanguEngine 最初探索自研 PanguBuild，讨论过不同实现语言、`Plugin.toml` / `Module.toml` 等配置形式，以及跨平台工具链和元数据生成的统一编排。但自行研发并长期维护完整构建系统，会引入较大的持续成本。

PanguEngine 以 C++23 和全平台 Clang 为基础，仍需要正确表达 Module、Plugin、工具、ThirdParty 的构建关系，并接入元数据与 ABI 兼容性检查。

## Decision

**采用 CMake 构建引擎、工具和第三方依赖，而不是维护自研的完整底层构建系统。**

此决策只冻结构建底座，不自动确认具体项目布局、CMake 函数、Preset、Target 映射、Manifest 格式或工具的最终进程边界。

## Consequences

- CMake 成为统一构建表达的基础；采用成熟的生成器、工具链和增量依赖机制。
- 早期“PanguBuild 必须自研完整构建核心”的路线被替代。
- `Module.toml` **不再自动是每个 Module 的必需文件**；`CMakeLists.txt` 与剩余模块元数据如何分工须另行决策。
- Plugin 的分发、安装与运行时发现仍可能需要独立 Manifest，不能简单用构建系统替代。
- `PanguBuild` 名称是否保留为上层工具、PanguClang/PanguMeta 如何接入、ABI Fingerprint 何时生成，均未冻结。

## Open Questions

CMake Presets、编译选项政策、Module 到 Target/镜像的映射、Plugin 打包、运行时 Manifest、元数据生成依赖和五平台工具链。

详细实施约束以 [build-system.md](../foundation/build-system.md) 为准。
