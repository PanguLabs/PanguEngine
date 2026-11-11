# Pangu 工具链与元数据工具

> 状态：Partially Frozen（通用工具链、平台编译器选型及独立 PanguMetaTool 路线已确定；首版 Meta 集成已实现，完整 ABI/发布集成仍待完善）
> 更新：2026-11-11
> 决策：[ADR-0004](../decisions/0004-platform-clang-toolchains.md)、[ADR-0017](../decisions/0017-standalone-pangu-meta.md)

## 统一约束

1. 所有目标平台使用 **C++23 + Clang**，禁用 C++ Exception；不得使用 `libstdc++`。
2. **Windows 使用 MSVC STL**；Linux、macOS、Android、iOS 使用与目标工具链及 SDK/NDK（适用时）配套的 **`libc++`**。
3. 构建底座为 **CMake 4.0+**；不 Fork `llvm-project`。有关 CMake Target、Preset、Toolchain 和构建依赖的契约见 [构建系统](build-system.md)。
4. **编译器版本按平台选择**，不是全平台统一 LLVM23；配置阶段必须验证编译器类型、版本及目标 Toolchain 来源，不得在 Android 上使用通用 LLVM23 检查。
5. 编译器与标准库/SDK/NDK 的基础选择由 CMake Preset/Toolchain 统一完成（Linux 与 macOS 使用专用 Toolchain，iOS 使用既有 Device Toolchain，Android 使用 NDK Toolchain，Windows 使用 Clang/MSVC STL Preset）；普通 ThirdParty C++ Target 不需要链接 `PanguBuildOptions` 才获得这一配置。
6. 同树 Module 遵守统一工具链、ABI 关键配置与所有权契约，不运行自制 AbiProfile；独立交付 C++ 二进制需在接纳边界验证真实配置与 API 契约。旧 [ABI Profile v1](abi-profile-fingerprint.md) 已退役，当前政策见 [ADR-0013](../decisions/0013-native-build-abi-boundary.md)。
7. Pangu 的扩展属性使用 `pga` 命名空间，反射标记不使用 `PG_CLASS`、`PG_PROPERTY` 等宏；属性和代码生成的有效性必须按实际目标编译器验证，详见 [Reflection / Meta](../core/reflection-meta.md)。

## 目标平台编译器选择

| 平台 | 编译器约束 | C++ 标准库 | 实施细节 |
| --- | --- | --- | --- |
| Windows | LLVM Clang 23.x | MSVC STL | [平台支持](platform.md) |
| Linux | LLVM Clang 23.x | libc++ | [平台支持](platform.md) |
| macOS | MacPorts LLVM Clang 23.x | libc++ | [Apple 工具链](apple-toolchain.md) |
| Android | **所选 NDK 自带 Clang**（不限定 LLVM23） | NDK libc++ | [Android 工具链](android-toolchain.md) |
| iOS | MacPorts LLVM Clang 23.x（默认；AppleClang 仅用于兼容性对照） | iPhoneOS SDK libc++ | [Apple 工具链](apple-toolchain.md) |

对 Windows、Linux、macOS、iOS：CMake 校验 `CMAKE_CXX_COMPILER_ID == Clang` 与 `23.0 <= CMAKE_CXX_COMPILER_VERSION < 24.0`；macOS / iOS 还需显式选择 MacPorts 工具链。Android：校验使用选定 NDK 的 Clang，并通过实际 C++23 编译验收；不单凭版本号推断兼容性。

**平台版本、路径、SDK/NDK、具体命令及实测结果不在此重复维护**：

- Android NDK、Gradle、CMake、ABI 和验证记录见 [Android 工具链](android-toolchain.md)。
- MacPorts、macOS、iPhoneOS SDK、编译链接与部署验证见 [Apple 工具链](apple-toolchain.md)。
- 目标环境和支持程度见 [平台支持基线](platform.md)；“目标支持”不代表已完成完整引擎构建。

## 独立 PanguMetaTool（首版生产集成已实现）

采用宿主独立 **PanguMetaTool**，内部 **Attr** 负责属性注册、Clang AST 提取和校验，**MetaIR** 保存规范化语义与构建期来源，**CodeGen** 按实际消费者生成普通辅助 C++。`pg::meta` 定义统一基础语义与按需目标接口，`pg::meta::ir` 提供构建期拥有数据的扩展表示；共享语义不要求宿主/目标对象布局相同。不再单独交付 PanguClang Plugin/EXE，不修改目标编译器，不生成替代原始源码的编译视图。详细分工、测试矩阵与未验收边界以 [Reflection / Meta](../core/reflection-meta.md) 为单一权威，取舍见 [ADR-0017](../decisions/0017-standalone-pangu-meta.md)。

MetaIR 与 Clang AST 生命周期解耦，BuildRoot、编译参数和 Host 装配由独立 GenerationRequest 表达；IR 的目标相关事实须关联实际目标上下文。目标 TypeInfo/FieldInfo 由生成代码经目标编译器构造，视图引用目标产物自身存储，不能保存宿主工具对象的内存、地址或布局。首版不增加 Meta 文件、Exporter 或磁盘格式依赖；构建期诊断与未被消费者需要的信息可止于内存模型。

属性以具体 `XxxAttribute` 为主体，源码采用去后缀的 CamelCase，如 `pga::Type` / `pga::Field`。多位置参数与类型化 Options 已有追加 PoC；裸 `ReadOnly = true` 不由现有通用属性接口自动支持。首版 Type/Field 处理器及 Options 常量绑定已实现，通用注册器和属性基类仍待定稿，不把 `ParsedAttrInfo` 误作所有业务属性值类型必须继承的基类。细节与证据统一见主要规范第 4、8 节。

首版输入与消费行为已通过 grilling 确认，细节见[首版规格](../../docs/superpowers/specs/2026-11-09-pangumeta-tool-design.md)：每 Module 一套明确上下文；无法从原始头还原的 PCH/Modules 依赖拒绝。项目自有反射声明及属性参数不得依赖两端有差异的 Clang 版本条件或 `__has_cpp_attribute(pga::...)`，普通目标平台/架构/配置宏仍可用；对已识别的不支持形式诊断，并做目标消费对照，不声称任意源码跨前端完全等价。

生成产物按 **Engine 与 Game Project 源码归属分别留在各自构建根**：Engine 自有代码位于 `<EngineBuildRoot>/Generated/PanguMeta/<Config>/<CMakeTarget>/`，Project 自有代码和 Host Catalog 位于 `<ProjectBuildRoot>/Generated/PanguMeta/<Config>/<CMakeTarget>/`；Project 引用 Engine 源码不改变 Engine 产物的存放归属。`CMakeTarget` 是实际编译生成实现的库或宿主目标名，不是逻辑 Module 名或 Alias。代码目录为 Public / Private，头/实现后缀为 `.gen.h` / `.gen.cpp`；全部生成实现编译进所属 Module/Host，不新增 Source 层或独立 Meta 库。原始类型头默认不包含自己的生成头，伴随生成头只由实际需要的消费者包含。一个 Engine 工作区对应一个当前 Game Project，切换 Project 时重新 Configure 并按新配置更新 Engine 构建产物。目录、依赖与 Project 切换要求见 [Reflection / Meta 第 6 节](../core/reflection-meta.md#6-生成代码存放编译与原生构建依赖)，生产接线已实现，实际覆盖见正式验收。

目标侧保留 `-Werror`，只对确实解析 `pga` 的目标及必要消费者增加 `-Wno-unknown-attributes`；这是整个诊断组的豁免，不是仅匹配 `pga`。Meta 阶段不得继承豁免，必须严格检查并传播驱动/前端错误。正式接线已按目标和必要消费者设置该选项，Meta 编译上下文归一化会移除该豁免并严格检查；三桌面和 ARM64 移动目标的实际覆盖见正式 Meta 验收。首版仅支持配套 SDK 与单配置 Ninja 上下文，不承诺任意跨 Clang 版本语义等价。

PanguMetaTool 及所链接的 LLVM/Clang C++ 库必须匹配宿主标准库和 ABI；Linux/macOS 使用 libc++，Windows 使用 MSVC STL。必要时可显式重建兼容开发库，不等于 Fork LLVM 或重建目标 clang；禁止让日常引擎构建隐式重建 LLVM。Linux 已通过全 libc++ 依赖链验证；Windows 正式兼容 SDK 已保留 RTTI 并通过真实预检，不保留 PoC 的临时禁用例外。

启用任一 PanguMetaTool 任务均要求准备完整工具及匹配 SDK，包括仅生成 Query/Catalog 的目标；没有 Type/Field 输入不代表免除该依赖。首版提供正式显式 SDK 准备流程与版本/ABI/RTTI 检查，必要时提供依赖库构建步骤，新检出不依赖探针或旧缓存；首版不交付预编译 SDK 安装包。

Android/iOS 由桌面 PanguMetaTool 按实际目标编译上下文解析，再由 NDK Clang/MacPorts Clang 编译生成结果。宿主前端的资源头与目标 SDK/NDK 的系统、标准库头分别配套；工具不进入移动应用。已有目标构建侧验证不代表所有语言/SDK 语义兼容或设备运行通过。

本轮 PanguMetaTool 的 Android 实现与验收仅 ARM64（arm64-v8a），不增加其他 Android 架构工作；iOS 为 Device ARM64。历史探针的四 ABI 记录不扩展当前任务范围，三桌面与移动端的运行/编译/最终链接边界以首版规格 §8 为准。

## 尚未冻结

现有 CMake Preset/Toolchain 已完成最小构建接线；编译器补丁版本与 SDK 升级政策、平台 ABI/Runtime 参数、PanguMetaTool 正式 API/Taxonomy、完整通用 Driver 参数、多配置、CI 和产品发布验证仍待完善。独立工具路线不再列为未决事项；已验证能力与剩余范围见其权威专题文档。

正式 SDK 准备入口为 BuildTools/PanguMetaTool/Sdk/Prepare.cmake，消费准备好的 PanguMetaSdk.cmake。实际编译器/SDK 版本、三桌面与 ARM64 移动结果见[Meta 验收](../../docs/superpowers/plans/2026-11-09-pangumeta-tool-validation.md)。编译器版本以实际 binary 为准，不以旧 CMake cache 推定。
