# Pangu 构建系统

> 状态：Partially Frozen（采用 CMake 4.0+、单 Module 单库构建规则已冻结；具体构建 API/产物协议待设计）
> 更新：2026-10-21
> 决策：[ADR-0001：采用 CMake](../decisions/0001-adopt-cmake.md)、[ADR-0005：一个 Module 一个库](../decisions/0005-one-module-one-library.md)

## 已确定的决策

- **使用 CMake 作为构建系统基础**，不自行维护完整自研构建内核。
- **最低 CMake 版本为 4.0**；新建或修改的 `CMakeLists.txt` 统一使用 `cmake_minimum_required(VERSION 4.0)`。
- C++ 编译器、标准库与平台 SDK/NDK 遵守 [Toolchain](toolchain.md)。Android 使用 NDK Clang，不能套用其它平台 LLVM Clang 23.x 的版本检查。
- **一个 Pangu Module MUST 对应唯一的实际 CMake Library Target**（`STATIC` 或 `SHARED`），编译后只产生一个主要静态库或动态库；Module 可以包含多个 `.cpp` Translation Unit。
- 不允许把一个 Module 拆成多个 Module Library Target，也不允许多个逻辑 Module 合并定义为同一个 Module Library Target。Meta 生成注册代码应加入同一 Module Target。
- **Plugin 是功能分发/启用单位，不要求有自己的库目标**，可以包含多个 Module 或只包含 Assets。
- ThirdParty 可以是独立的 CMake 目标，并被 Pangu Module 依赖；不强制使用 Package Adapter，也不要求每个 ThirdParty 目标都成为 Pangu Module。
- CMake 是构建关系的权威来源；Plugin 的安装、发现和版本信息仍可能需要独立 Manifest，不因此形成第二份构建配置。

## Monolithic / Modular 与 Module 库类型的边界

**已确定的设计约束**：`STATIC` / `SHARED` 是 CMake 原生的 **Library Target 类型**，直接描述每个 Pangu Module 的编译、链接和产物；`Monolithic` / `Modular` **不是 Module 类型，也不是新的引擎架构层**。**只有在需要对整个 Engine / Game / Editor 统一施加部署或链接策略时，才将 Monolithic / Modular 作为顶层 CMake 构建选项**（如 Preset 或顶层 Cache 配置），不要求每个工程必须声明或选择该选项。

- 顶层策略可影响 Module 的默认 `STATIC` / `SHARED` 选择及最终装配方式，但不能取代每个 Module 最终落实为唯一真实 CMake Library Target 的既有规则；允许因平台或功能需要显式混用两类目标。
- `Monolithic` 不等于禁止所有动态 Plugin，`Modular` 也不等于所有 Module 必须为 `SHARED`；两者描述的是整体部署取向，而非逐 Module 类型。
- CMake 内置的 `BUILD_SHARED_LIBS` 仅控制**未明确指定**库类型的 `add_library()`，不把它误当成 Pangu 的全局部署协议，也不无差别覆盖 ThirdParty 的库类型。
- 顶层选项的**实际名称、默认值、Preset 组织、平台映射与是否需要实现**尚未冻结；本次仅确定其适用层级和按需引入原则。

## “一个 Module 一个库”的范围

| 事项 | 冻结约束 |
| --- | --- |
| Module 构建 | 一个实际 CMake `STATIC` 或 `SHARED` Library Target |
| CMake `ALIAS` | 只是 Target 别名，不是第二个构建目标 |
| 源文件 | 多个 Translation Unit 允许；不强制 Unity Build |
| 生成代码 | 作为该 Module 目标的源文件，不创建第二个 Module 库 |
| 主要产物 | 一个 `.a` / 静态 `.lib`，或一个 `.dll` / `.so` / `.dylib` |
| 辅助产物 | Windows 的 import `.lib`、PDB 等不算第二个 Module |
| STATIC 运行期 | 最终链接到 EXE 或动态库；归档本身不能被单独卸载 |
| SHARED 运行期 | Module 加载器直接管理库句柄和 CodePin，不另建 BinaryImage 架构层 |
| Host/Tool/ThirdParty | 可有自己的普通 CMake Target，但不因此违反 Pangu Module 规则 |

对于 STATIC Module 被链接进可卸载动态库的情形，代码驻留保护 **MUST 覆盖实际承载代码的动态库**；单库构建关系不等同于独立运行时可卸载性。

## 与旧方案的关系

1. 完整自研 `PanguBuild` 构建核心已被 CMake 替代；`PanguBuild` 是否保留为上层工具仍待决定。
2. `Module.toml` 不再是每个 Module 必需文件；`CMakeLists.txt` 是编译和链接配置的权威来源。分发清单 `Plugin.toml` 是否沿用及其字段仍未冻结。
3. 旧方案允许 Module 和多个 CMake Target / 物理镜像之间灵活映射；**ADR-0005 已收紧为一个 Module 对应一个库 Target 和一个主要产物**。
4. 不建立独立 BinaryImageManager / BinaryImageDescriptor；物理库文件和 CodePin 是现有 Module 管理机制的内部实现细节。

## Compile-Time / Build-Time First 的构建职责

按 [C++ 规范与最佳实践](cpp-guidelines.md)，类型与静态分发应尽量在编译期解决；**Module 依赖、编译器/标准库配置、生成代码、Shader/Asset 预处理和 ABI 兼容性采集**等更适合在 Build-Time 完成。它们属于 CMake 与配套工具的协同职责，并不要求恢复已废弃的自研 PanguBuild 构建内核。

- CMake 仍是构建依赖的权威来源；可由 PanguMeta 等工具按需要生成代码，生成源文件归入对应 Module 唯一库 Target。
- 编译器与目标平台配置应从**实际编译**获得并验证；ABI Fingerprint 的 Schema、RFC 8785 Canonical、SHA-256 和严格兼容政策已由 [ABI Profile v1](abi-profile-fingerprint.md) 冻结；但构建采集与实际拒绝实现仍未完成，不能声称已经实行全部 Build-Time ABI 拒绝策略。
- 预生成并不取代 Runtime 对外部输入、动态模块状态、设备能力以及资源版本的必要验证。
- 元数据生成、Shader/Asset Pipeline 和完整增量构建协议均为后续设计，不因通用最佳实践而自动视为已实现。

## 必须解决的设计问题（尚未冻结）

| 主题 | 待决问题 |
| --- | --- |
| CMake 组织 | 根工程、子工程、Preset、Toolchain、`pg_add_module()` 等包装 API |
| 库类型与部署策略 | `STATIC` / `SHARED` 是逐 Module CMake Target 类型；`Monolithic` / `Modular` 仅在需要统一部署策略时作为顶层 CMake 选项。具体默认库类型、Preset、平台策略与选项命名待决 |
| 产物布局 | 库、Manifest、符号文件、动态库导出、安装和分发目录 |
| Meta 生成 | Clang 属性提取、代码生成、增量依赖、多配置构建 |
| ABI | Profile Schema / Canonical / Fingerprint 算法与兼容判定已冻结；待实现的是每 TU 配置采集、生成器、装载前预检及二次校验的具体接口 |
| ThirdParty | Source/Binary 版本、目标导出与平台链接 |
| 交叉编译 | Windows / WSL Linux / macOS / Android / iOS Toolchain 与 SDK |

## 验证要求

后续至少验证 Module 与 Target 的一对一关系、产物数量、编译数据库、增量构建一致性、Meta 生成依赖、ABI 兼容配置、CodePin 以及平台矩阵。已有 PoC 不等于正式 Runtime / 多平台验收。
