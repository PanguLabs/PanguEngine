# ADR-0017：独立 PanguMetaTool 与受控属性诊断豁免

> 宿主 SDK 政策部分已由 [ADR-0018](0018-installed-pangumeta-sdk.md) 替代（2026-11-13）：复用已安装开发包；Linux 工具允许 libstdc++，Clang 适配代码匹配 SDK RTTI；本文相关旧条款仅保留历史背景。
> Status：Accepted（架构路线、MetaIR/统一语义边界、按需输出、具体属性类型、CamelCase 命名及生成代码存放/编译归属；具体模型/API、通用框架与生产接线待完成，PoC 不代表生产交付）
> 日期：2026-11-08；依据的验证记录：2026-11-07～08。
> 更新：2026-11-09，明确 `PanguMetaTool` 命名、单当前 Game Project、MetaIR 与统一语义/跨目标边界；生成目录与历史 PoC 命名保持不变。
> 主要规范：[Reflection / Meta 与 PanguMetaTool](../core/reflection-meta.md)、[工具链](../foundation/toolchain.md)

## Context

PanguEngine 要在原始 C++23 中保留 `[[pga::...]]`，不使用反射标记宏，继续严格检查普通 C++ 警告，并支持 Windows、Linux、macOS、Android 和 iOS。需要按需生成 Type/Field 元数据及后续 Module/Catalog，而不再开发一套构建系统。

早期研究了 Clang 动态插件、Windows 内置扩展、定制编译器静默接受属性，以及生成编译视图。macOS/Linux 的最小插件验证可行，但 Windows 所用官方 SDK 的开发库可用并不等于目标编译器提供插件符号导出。用户随后明确接受目标侧 `-Wunknown-attributes` 的受控豁免，因此不再需要通过编译器改造来满足合法属性编译。

独立程序在自己的进程内链接 Clang 库、登记属性处理器并分析原始源码，已经完成三个桌面宿主及 Android/iOS 构建侧验证。具体环境、检查数量、证据位置与局限只在主要规范维护。

## Decision

1. **交付形态选择独立 PanguMetaTool Build Tool**。可执行文件与 CMake Target 均为 `PanguMetaTool`，源码根目录为 `BuildTools/PanguMetaTool/`，与 `PanguBuildTool` 命名一致；元数据领域仍称 Meta，命名空间为 `pg::meta`，生成目录保留 `Generated/PanguMeta/`，历史 PoC 名称及验证路径保持不变。前端组件命名为 **Attr**，负责属性识别、Clang AST 提取与检查；CodeGen 消费内部模型生成普通 C++。不再要求独立 PanguClang 程序或目标编译器插件。
2. **使用现有平台目标编译器**，不 Fork LLVM、不修改编译器语言语义、不要求自编译目标 clang。允许显式准备或重建与宿主工具 ABI 匹配的 LLVM/Clang 开发库。
3. **目标编译保留 `-Werror`，对必要目标增加 `-Wno-unknown-attributes`**。这是整个未知属性诊断组的豁免，不冒充仅对 `pga` 的白名单，也不设置为所有 ThirdParty 的无条件默认策略。
4. **Meta 检查不得继承该豁免**。错误属性和驱动/前端错误必须阻断构建；不能以目标编译通过代替 Meta 校验。扫描覆盖范围与失败产物处理需在生产集成中证明。
5. **宿主工具与目标上下文分离**。PanguMetaTool 在桌面宿主运行，按真实目标编译配置解析；移动产物不链接宿主 LLVM/Clang。Windows 使用 MSVC STL，Linux/macOS 工具及 C++ SDK 使用 libc++，移动端使用配套目标 libc++，不接受 Linux libstdc++ 例外。自有工具代码（含 Attr）保留 RTTI；Windows PoC 对 Attr 临时使用 `-fno-rtti` 尚不满足此要求，正式集成须通过启用 RTTI、ABI 匹配的开发库消除此问题。
6. **CMake/Ninja 负责依赖与增量**。生成代码附着现有 Module/Host Target，不写回原始源码，不引入编译视图、第二个 Module Library、自制调度器或逐 TU 收据。
7. 首版目标范围保持 **Module/Catalog 自动生成 + Type/Field 核心元数据**；仅后者的受限链路已经由 PoC 验证。Private 无侵入自动读写继续单独研究，不因 AST 可见就宣称已经实现。
8. **以具体 `XxxAttribute` 组织属性，源码采用去掉 `Attribute` 后缀的 CamelCase**：`FieldAttribute → pga::Field`、`TypeAttribute → pga::Type`，不以 `PgaAttribute<bool>` 作为长期扩展方式。历史小写探针不自动成为正式兼容别名；参数形式、通用注册/适配 API 和实例存储仍需分别设计。
9. **生成代码按 Engine / Game Project 源码归属分别存放**：Engine 自身 Module 及 Engine Plugin 的元数据位于 `<EngineBuildRoot>/Generated/PanguMeta/<Config>/<CMakeTarget>/`，Game Project 自身 Module、Project Plugin 与 Game/Editor Host Catalog 位于 `<ProjectBuildRoot>/Generated/PanguMeta/<Config>/<CMakeTarget>/`；即使 Project 通过 `add_subdirectory()` 引用 Engine 源码，也不把 Engine 生成代码写入 Project 构建树。`CMakeTarget` 是实际编译生成实现的库或宿主目标名，不是逻辑 Module 名、Alias 或 UE/UBT 应用级 Target。头/实现统一为 `.gen.h` / `.gen.cpp`，代码目录保留 Public / Private，不设额外 Source 层；公开生成头进入 Public，内部头及所有生成实现进入 Private。Module 实现和汇总编译进原库，Host Catalog 编译进对应 Host；不新增 Meta 库，也不写入业务 Source。
10. **原始类型头默认不包含自己的生成头**：生成 `.gen.cpp` 包含原始声明；只在需要模板、编译期接口或生成声明时提供按需包含的伴随 `.gen.h`。默认按输入头组织生成实现，不要求业务源码手动包含 `.gen.cpp`。生成任务粒度、参数文件、Depfile/stamp 和多配置接线仍需实施验证。

11. **一个 Engine 工作区对应一个当前 Game Project**。切换 Project 时重新 Configure，复用 Engine 构建目录并按当前 Project 配置更新、覆盖之前的生成和编译产物；同一 EngineBuildRoot 的 Configure/build/test 会话串行，切回旧 Project 同样必须刷新当前请求与失效依赖。既有平台构建根、Config 和实际目标目录继续适用。
12. **MetaIR 是现有内部 Meta 模型，使用拥有数据的纯 C++23 内存表示**。`pg::meta` 定义统一基础元数据语义及按需可供 Runtime 消费的 TypeInfo/FieldInfo 等接口；`pg::meta::ir` 保存构建期专属信息，执行跨声明汇总、规范化和生成准备。共享语义而非强制相同对象布局，具体成员与 ABI 未冻结；不保留 Clang AST 借用，不另建 Attribute Taxonomy。
13. **按实际消费者生成和保留代码/描述**。构建上下文、路径与 Host 装配由独立 GenerationRequest 提供，MetaIR 明确关联有效目标上下文。宿主生成 C++ 定义，由目标编译器构造目标描述，不能把宿主对象内存、指针或布局直接保存成目标运行时结构。首版 IR 仅在内存中使用，不新增 Meta 文件、Exporter 或磁盘协议；在独立消费者出现后再设计格式与版本。
14. **首版消费行为经 grilling 确认**。Foundation 只承载不依赖 Module/EngineHost 的轻量语义；无 Service Module 也须经生成类型入口取得仅移动、自持正确 CodePin 的短期描述借用，生产身份桥尚待新增。ModuleId 由公共 C++ 常量唯一定义，Definition 与生成入口共用，CMake 不复制值。按头登记消费需求、按 Module 汇总，头所有者唯一；孤立 Field 和漏登记反射头报错。完整 Q1–Q24、字段访问、支持范围、SDK 依赖和验收边界见[首版规格](../../docs/superpowers/specs/2026-11-09-pangumeta-tool-design.md)。

目录示例、输入路径防冲突、Project 切换、增量要求及迁移边界统一见 [Reflection / Meta 第 6 节](../core/reflection-meta.md#6-生成代码存放编译与原生构建依赖)。该选择借鉴 UE5 的产物归属，不引入其类内宏注入要求；现有探针、Export/Query 生成代码尚未迁移。

本轮同时落实 Engine 对象、库、导出头与 Meta 的实际分根，旧 Project 构建通过原生依赖重配恢复或失败；SDK 明确准备，Query/Catalog-only 任务也有工具依赖。Android 本轮仅 ARM64，历史其他 ABI 的验证记录保留其历史范围。

## 后续参数验证与设计建议（不扩充 Accepted 范围）

2026-11-08 的后续验证确认：多位置参数可行；裸 `ReadOnly = true` 在现有 `ParsedAttrInfo` 通用路径下不会自动获得 C# 命名绑定；真实 `pga::Field(Id, FieldOptions{...})` 的类型化对象参数在桌面及移动构建侧通过。Options 是一个位置参数，其内部使用 C++ 指定成员初始化，不是新增属性专用语法。具体检查矩阵与受限范围见主要规范第 8 节。

大量属性的推荐组织是具体类型、参数描述、通用绑定器和内部 Clang 适配器，使用集中入口及分组类型清单。`ParsedAttrInfo` 是当前 Clang 注册边界，不必成为每个属性值类型的公共基类；TypeInfo/FieldInfo 与构建期扩展表示的当前职责以 Decision 中的 MetaIR 决策为准，均与属性值、Options 分离。这些通用结构尚未实现，未批准“新增类即可自动发现”或无需重编工具的第三方属性插件机制。

位置参数、独立属性组合与类型化 Options 可按语义选用；不因 Options 探针通过就冻结所有 Attribute 的最终签名、默认值或构造绑定。现有两成员 Options 专用实现也不能被描述为完整 C# 特性系统。

## Alternatives 与替代关系

| 历史候选 | 本次不采用的原因 |
| --- | --- |
| 三平台动态 PanguClang Plugin | 不再为目标编译器引入插件 SDK、导出接口及二进制版本配套要求 |
| Windows 内置完整扩展，其他平台动态插件 | 不将属性规则变化绑定到定制 Windows 编译器产物更新 |
| 定制 Clang 仅静默忽略 pga，独立工具生成 | 已接受诊断组豁免，不再维护命名空间静默策略的编译器补丁 |
| 独立工具生成去属性源码视图 | 不引入原始源码与编译内容同步、调试映射及额外依赖层 |
| 插件直接 CodeGen 或插件内置 Meta Core | 不作为当前工具装配方式；职责分层保留在独立 PanguMetaTool 内 |
| Linux 工具暂用 libstdc++ | 与用户要求及统一工具链政策冲突；已由 libc++ 依赖库重建验证替代 |

上述是本专题的历史探索，不将其他 ADR 整体标为 Superseded。ADR-0004 的平台工具链、ADR-0013 的原生增量原则、ADR-0015/0016 的 Module/Host 行为继续有效；本 ADR 只确定 Meta 工具路线，不恢复历史混合链接模式。

## Consequences

收益是无需插件加载或目标编译器补丁，也无需转换原始源码；同一 Attr/CodeGen 逻辑可在不同宿主上构建，移动端继续使用既定 NDK/SDK。属性体系的演进集中于独立工具。

代价是目标侧其他未知属性也可能被豁免；PanguMetaTool 必须成为受控输入的必经检查。Clang API/SDK 升级、跨前端语义一致性、依赖库标准库与 RTTI 配套、目标参数归一化和宿主工具分发仍需维护。独立 EXE 不承诺无外部依赖，两个阶段也不先验保证更快。

Linux 全 libc++ 方案重建了 LLVM/Clang 静态依赖闭包，但没有重建或替换目标编译器；准备 SDK 必须是显式步骤，不能使日常引擎构建隐式重建 LLVM。

已验证的是桌面原型运行和移动目标编译链接。Android 四 ABI、iOS ARM64 的宿主检查不是设备测试；签名、打包、JNI/App 启动、真机或模拟器运行未因此通过。

## Open Questions

首版 Field 参数、输入范围、产物归属、受保护描述、Project 切换、失败发布与性能验收行为已确认；具体 C++/请求 API、跨声明合并、Module/Catalog 输出、Depfile/编译数据库、恢复接线、Windows RTTI 开发库接入和正式性能基线仍待实现与验收。完整 Attribute Taxonomy、持久化身份演进、动态 Registry、Private 无侵入访问、SDK 发行/升级等属于后续专题。前端差异的首版输入限制已明确，对支持范围仍须实际验证，不能视为任意源码兼容保证。

日常实施与状态更新以 [Reflection / Meta](../core/reflection-meta.md) 为单一权威；不把未版本化 `.probe/` 当成正式回归测试的永久依赖。
