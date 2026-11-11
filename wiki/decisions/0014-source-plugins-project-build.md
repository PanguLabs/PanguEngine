# ADR-0014：源码 Plugin 与独立 Project 构建边界

> Status: Accepted（设计决策；不等于工具及五平台验收已实现）
> 日期：2026-11-04
> 替代范围：[ADR-0013](0013-native-build-abi-boundary.md) 的独立 **Pangu** 原生 Plugin/SDK 二进制交付及装载前 Preflight 路线；部分修订 [ADR-0010](0010-unified-cpp-module-entry.md) 的同一交付范围
> 设计依据：[源码 Plugin 与独立 Project 构建设计](../../docs/superpowers/specs/2026-11-02-source-plugins-project-build-design.md)

## Context

PanguEngine 的 CMake 4.0+ / Ninja 与 C++23/Clang 单图构建已替代自制 PanguBuild、逐 TU ABI 证据；既有 ADR-0013 为单独交付的 Pangu C++ 插件/SDK 规划了可信二进制接纳及 Preflight。然而维护多套二进制兼容证明和交付工具，会重建复杂的 ABI/构建边界，并可能使普通增量编译失效。当前产品路线选择**源码交付 Pangu Plugin**，由真正的 Engine 或 Game/Editor 消费者在其构建配置下统一编译。

## Decision

1. **源码交付**：Pangu 原生 Plugin 仅以源码形式纳入 Engine 或独立 Project 的构建图；不提供预编译 Pangu Plugin/SDK 供另一既有宿主独立接纳的工作流。原 ADR-0013 为此预留的 `config-v2`、可信产物绑定、TOCTOU 防护与 OS Load 前独立 ABI Preflight **不再是 Pangu Plugin/SDK 产品目标**，不恢复 AbiProfile。历史 ADR 不删除。
2. **工程所有权**：Engine 可独立以顶层 CMake 构建/CTest；独立 Game Project 也可持有顶层 CMake/Toolchain/CTest，以 `add_subdirectory(<EngineSource> <ProjectBuild>/Engine)` 纳入 Engine 源码。每个顶层拥有自己的独立 BuildRoot 和一个 CMake/Ninja 目标图；平台 Clang/标准库/SDK 统一由其顶层选择。Project 正式构建锁定 Engine Git Commit 与干净工作树，本地联调允许显式 override 并报告版本差异。Engine 子目录不得重新初始化全局 Toolchain、CTest 或 BuildOptions。
3. **固定源码发现**：Engine `Plugins/` 与可选 Project `Plugins/` 是唯一两个来源，只扫描直接子目录。每个插件必须同时包含 `Plugin.toml` 和 `CMakeLists.txt`；Manifest 仅描述身份/展示元数据，CMakeLists 是真实构建依赖唯一权威。`plugin.id` 全局唯一、目录无关，v1 格式 `^[a-z][a-z0-9]*(?:-[a-z0-9]+)*(?:[.][a-z][a-z0-9]*(?:-[a-z0-9]+)*)*$`；`name` 可选字符串，`version` 可选 SemVer 2.0.0。未知键、损坏 TOML、重复 ID 和文件不完整，即使插件未启用也配置失败。
4. **最小 Build Tool**：顶层每次 Configure 都独立以**宿主** Clang/C++23 配置、请求构建并运行 `PanguBuildTool`，即使没有任何插件也必须成功返回空 JSON；解析使用固定版本 `toml++`、`TOML_EXCEPTIONS=0`。Android/iOS 交叉编译的宿主工具不继承目标平台 Toolchain。无变更的普通 Ninja 不扫描插件或构建工具；依赖失效由 CMake/Ninja 自身负责。
5. **消费者驱动构建**：配置期发现不等于启用；显式 `PANGU_ENABLED_PLUGINS` 依 Plugin ID 选择，并以 `add_subdirectory(... EXCLUDE_FROM_ALL)` 建立候选 Target；启用不等于实际编译、部署或运行时激活。仅 Game/Editor/Tool 的显式链接与依赖引入 Module。无第二 Plugin 依赖 DAG、无自动递归启用、无全局 Plugin Catalog、无通用 `POST_BUILD`。SHARED 需要显式目标依赖与部署规则，不能把文件复制视为 OS Load。
6. **Module/ThirdParty 边界不变**：每个 Module 仍仅一个真实 `STATIC`/`SHARED` Library；`Pangu::` 只表示 Module ALIAS，`ThirdParty::` 只表示第三方依赖。STATIC 保留 Host 显式 Catalog，SHARED 保留统一原生 C++23 Query、仅用于定位的 `extern "C"` 稳定导出名、`ModuleId`/依赖/注册回滚、ModuleCodePin 与安全卸载。源插件的 `Plugin.toml` 不充当运行时激活协议，不新增 BinaryImage 层或稳定 C Bootstrap。
7. **明确保留的外部二进制**：这项政策仅针对 **Pangu 自有源码 Plugin 与独立 Pangu SDK 交付模式**；系统 SDK/运行库、操作系统 Framework、普通预编译 ThirdParty SDK/库仍按各自 Toolchain、供应商及 ABI/Ownership 约束接入，可使用 `IMPORTED` 等普通 CMake Target。它们不是必须提供源码的 Pangu Plugin，也不因此自动获得 Pangu 通用 Preflight 机制。
8. **测试与性能闸门**：Tests 不运行 AbiProfile；保留 Engine standalone 五平台配置及桌面 CTest，验证 Project 两根发现/启用与真实消费者、负例、SHARED 显式部署。零变化 Ninja 必须无多余编译/链接/工具扫描，变更未消费 Plugin 不应使无关 Module 重编；记录新旧 A/B 样本，含首次工具编译成本，不以单次冷启动代表稳定态。

## Consequences

- 构建与依赖决策回归 CMake/Ninja，不维护独立 Pangu 二进制发布的 ABI 证明工具；代价是**不支持**把不同 Pangu 构建的原生 Plugin DLL 临时混入已有 Host 并假设安全。安装、卸载和运行时加载仍受 CodePin/生命周期约束，不能因源码同构建而省略。
- 新的 `Plugin.toml` 和 `PanguBuildTool` 是**目标方案**；在实际代码、CTest 和各平台产物落地前，不应宣称功能已经实现。历史 ADR-0013 对普通增量构建的决定继续有效，但其独立 Pangu Plugin/SDK 未来交付分支由本 ADR 精确替代。
- 与本 ADR 同步修订 [Plugin/Module](../core/plugin-module.md)、[ABI](../foundation/abi.md)、[构建系统](../foundation/build-system.md)、[ThirdParty](../foundation/third-party.md) 的现行规范；不篡改已经发生的 ABI Profile 历史实验和验证记录。

## Open Questions

`PanguMetaTool` 自动生成 Catalog 的协议、运行时插件启停清单、SHARED 实际 Loader/热重载、资产管线、完整安装打包及设备级动态库装载策略待分别设计。以上都不构成恢复独立 Pangu 原生二进制 SDK/Plugin Preflight 的授权。
