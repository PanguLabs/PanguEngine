# Pangu 构建系统

> 状态：Partially Frozen（采用 CMake 的决策已确定，具体构建契约待设计）
> 更新：2026-10-20
> 相关决策：[ADR-0001](../decisions/0001-adopt-cmake.md)

## 已确定的决策

- **使用 CMake 作为构建系统基础**，不继续自行维护一套完整的自研底层构建引擎。
- **最低 CMake 版本为 4.0**；新建的 `CMakeLists.txt` 统一写 `cmake_minimum_required(VERSION 4.0)`，不再使用 3.x 最低版本约束。
- 所有 C++ Target 的编译器、语言标准与标准库必须遵守 [统一工具链规范](toolchain.md)；CMake 在配置阶段按目标平台验证实际工具链，不得把其他平台的版本断言套用到 Android。SDK、NDK 及具体编译参数由平台工具链文档维护。
- 构建模型需要能表达 Engine、Plugin、Module、ThirdParty 与工具程序之间的依赖，但它们**不必与 CMake Target 或单个动态库一一对应**。
- ThirdParty 可以由 CMake 目标及引擎层 Module 依赖进行集成，不强制为每个第三方依赖开发独立 Package Adapter。
- 代码生成、元数据和 ABI 兼容性检查必须能接入构建流程；接入机制尚未最终定稿。

## 与旧设计的关系

早期讨论过独立实现完整构建能力的 `PanguBuild`，以及以 `Module.toml` 描述每个模块。采用 CMake 后：

1. **完整自研构建核心的路线已被替代**。
2. `PanguBuild` 名称是否保留，以及是否仅承担 CMake 上层编排，**仍待决定**。
3. **不把 `Module.toml` 规定为每个 Module 的必需文件**；由 `CMakeLists.txt` 承载多少信息、是否另设模块元数据有待明确。
4. Plugin 分发和发现所需的 Manifest 与 CMake 构建描述是不同关注点，不可从使用 CMake 推导出不再需要 Plugin Manifest。
5. 旧 PanguBuild API、固定目录、格式示例均不能作为当前构建系统的已冻结接口。

## 必须解决的设计问题（尚未冻结）

| 主题 | 需决策的问题 |
| --- | --- |
| CMake 组织 | 根工程、子工程、Preset、Toolchain 文件和 Module 注册函数 |
| Target 映射 | Module / Plugin / 动态镜像与一个或多个 CMake Target 的关系 |
| 构建产物 | 编译输出、运行时清单、动态库、符号文件的布局 |
| Meta 生成 | Clang 属性提取、生成文件、增量依赖和多配置构建 |
| ABI 约束 | Profile 的字段、Canonical Serialization、Fingerprint 算法与装载校验 |
| ThirdParty | 版本、Source/Binary 依赖、源码或预编译依赖的复用与导出 |
| 交叉编译 | Windows、WSL Linux、macOS、Android、iOS 的工具链与 SDK 选择 |

## 验证要求

后续实施至少需要编译数据库、增量构建一致性、代码生成依赖、ABI 配置一致性及平台矩阵验证。目前仓库内是否存在对应可运行构建目标，应以源码而不是本文件判断。
