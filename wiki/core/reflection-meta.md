# Pangu Reflection / Meta 与独立 PanguMetaTool

> 设计状态：Partially Frozen（独立 Build Tool、Attr / MetaIR / CodeGen 分工、统一元数据语义与命名空间边界、按需输出、受控属性豁免、标准库、CamelCase 命名及生成目录/后缀/编译归属已确定；首版模型与生成协议已实现；Public API/ABI、通用注册/绑定与完整 Taxonomy 未冻结）
> 实现状态：正式 PanguMetaTool 已实现 scan/query/catalog、强类型访问、可选描述、Module Pin 与原生增量接线。实际平台与模式证据见[正式验收](../../docs/superpowers/plans/2026-11-09-pangumeta-tool-validation.md)；历史 PoC 不替代正式结果，也不代表移动设备运行。
> 更新：2026-11-11；归档验证日期：2026-11-07（基础链路）、2026-11-08（命名参数与 Options）。
> 决策：[ADR-0017：独立 PanguMetaTool 与受控属性诊断豁免](../decisions/0017-standalone-pangu-meta.md)

## 1. 已确定的方案与范围

**采用一个独立 PanguMetaTool 可执行程序，内部以 Attr 负责 Clang 语义提取与属性检查，以 CodeGen 负责普通 C++ 辅助代码生成。目标编译器继续使用既定平台工具链，只对需要解析属性的编译目标受控豁免未知属性诊断。**

1. 原始 C++23 源码保留 `[[pga::...]]`，不使用 `PG_CLASS`、`PG_PROPERTY` 等反射标记宏；C++ 命名空间以 `pg` 开头，`pga` 仅为属性声明域。
2. PanguMetaTool 在构建宿主运行，借助 LLVM/Clang LibTooling 与 AST 分析原始源码，不用正则表达式解析 C++。
3. 前端组件名称为 **Attr**，不再使用 ClangFrontend；不单独交付 PanguClang EXE 或需要目标编译器加载的 PanguClang Plugin。
4. 不 Fork 或修改目标 Clang，不要求其原生认识 `pga`；不生成替换原始源码的编译视图、影子头文件或 VFS Overlay。
5. 允许为宿主工具准备或重建兼容的 LLVM/Clang **开发库**。重建依赖 SDK 不等于重建目标编译器，也不允许隐式放宽标准库政策。
6. Windows 宿主使用 MSVC STL；Linux/macOS 宿主工具与所链接的 C++ SDK 使用 libc++。Android/iOS 目标使用配套 NDK/SDK libc++；不保留 Linux libstdc++ 例外。
7. CMake/Ninja 负责原生构建依赖与按需调用，PanguMetaTool 不成为第二套构建系统，不承担 Plugin 发现或运行时加载。

**首版已实现 Module Query/Host Catalog 自动生成与 Type/Field 核心元数据。** 首版不承诺完整 Serialization、ECS、Event/Message/Command、Asset/Resource 或 Inspector。

引擎使用统一的基础元数据语义服务上述领域，而不是各领域自行发展互不兼容的声明体系。统一语义不要求统一宿主/目标对象布局；具体属性签名、Runtime API、领域 Schema 与格式版本仍须分别设计。

## 2. 组件职责与数据流

| 组件 | 职责 | 不承担的职责 |
| --- | --- | --- |
| PanguMetaTool 入口 | 接收输入及目标编译上下文，组织检查与生成，返回可靠退出状态 | 重新发现全部工程、替代 CMake/Ninja 调度 |
| Attr | 在工具进程内登记 `pga` 处理器，使用 Clang 解析类型/字段，检查声明位置与参数，提取语义和诊断位置 | 改变目标编译器、自动绕过 Private、生成另一份业务源码 |
| MetaIR（`pg::meta::ir`） | 以拥有数据的强类型模型保存规范化语义与构建期来源，执行跨声明汇总和生成准备 | 借用 Clang AST、复制完整构建图、承诺磁盘协议或 Runtime ABI |
| GenerationRequest | 表达实际目标编译上下文、输入/输出与消费需求，以及 Module/Host 装配登记 | 成为类型自身的反射语义或第二份手工 Manifest |
| CodeGen | 按消费需求从 MetaIR / 装配请求生成普通 C++ 接口、实现及描述 | 再解析 C++、无条件保留全部描述、引用宿主工具临时存储 |
| 官方目标工具链 | 编译原始源码与辅助代码，完成实际目标链接 | 理解被豁免的 Pangu 属性领域语义 |

正式可执行文件与 CMake Target 统一命名为 `PanguMetaTool`，源码根目录为 `BuildTools/PanguMetaTool/`，与 `PanguBuildTool` 保持一致。元数据领域仍称 Meta，命名空间沿用 `pg::meta`，生成目录保留 `Generated/PanguMeta/`。历史 PoC 的产物名称及验证路径保留原样。

逻辑组织为 `PanguMetaTool / Attr / MetaIR / CodeGen`；MetaIR 是已有内部 Meta 模型的正式名称，不新增重复中间层。已确定具体 `XxxAttribute` 与去后缀的 CamelCase 属性命名，工具内部目录、通用框架 API 和命令行签名尚未冻结。基础原型产物名为 `PanguMetaProbe`，追加参数探针另有独立产物，均不能当作已交付的生产工具。

```text
原始 C++23 源码 + pga 属性 + 实际目标编译上下文
                    |
        宿主 PanguMetaTool：Attr -> MetaIR -> CodeGen
                    |
          按实际消费需要生成的辅助 C++
                    |
原始源码 --------> 官方目标 Clang --------> Module / Host 产物
```

### 2.1 统一语义、MetaIR 与跨目标表示（首版已实现，长期 API 未冻结）

`pg::meta` 定义统一的基础元数据语义以及需要时可在 Runtime 消费的接口，包括 TypeInfo / FieldInfo、TypeId 等轻量描述契约。`pg::meta::ir` 保存构建期专属信息，使用 C++23 强类型、拥有数据的 TypeDefinition / FieldDefinition 等表示，执行跨声明汇总、规范化和生成准备。具体成员已在 Foundation 公共支持头及工具 MetaIR 实现，公共布局不因此冻结。

**共享的是身份、类型关系、属性和访问约束的语义，不要求两阶段使用完全相同的 C++ 存储布局。** IR 可以拥有字符串、完整生成信息及源码位置，目标描述按需使用 string_view/span 等轻量视图；CodeGen 按明确映射生成 TypeInfo/FieldInfo，不维护另一套 Attribute Taxonomy、默认值或约束解释。

Attr 完成参数解析、默认值/常量求值及声明内校验后交付规范化语义。MetaIR 保存名称、可选稳定身份、字段 Key、类型/const/访问事实、经检查的属性语义及诊断来源，不保存 Decl*、QualType、ParsedAttr、ASTContext 或表达式树的借用；CodeGen 不依赖 ASTContext 的存活。跨 TU 重复声明须合并或明确诊断，同一输入与有效目标上下文的结果必须确定，不能取决于哈希容器遍历顺序。

BuildRoot、编译参数、生成路径及 Host Catalog 装配集合由独立 GenerationRequest 表达；IR 明确关联其适用的真实目标上下文，不把 Android 32 位和桌面 64 位的目标相关事实混用。宿主 PanguMetaTool 生成普通 C++ 初始化表达式与定义，由目标编译器构造目标 TypeInfo/FieldInfo；目标视图引用目标产物自身的存储。**不能把宿主对象内存、指针或布局直接保存成目标运行时结构。** 源码位置默认仅用于构建期诊断，宿主地址/字段偏移不作为持久身份或跨平台数据格式。

所有参与生成的属性在 Build 阶段严格检查；只有实际消费者需要的信息才生成和保留。强类型访问可仅需生成头，确需非内联实现或运行期枚举时再生成实现与描述表；仅供构建检查的信息可以止于 MetaIR。统一模型可支持未来多种输出，但首版仅在工具内存中存在，不新增 Meta 文件、Exporter 框架或 JSON/FlatBuffers/Protobuf 磁盘协议。派生生成请求即使采用 JSON，也不是 MetaIR 导出；在独立消费者出现后再设计数据格式、版本与目标信息表达。

### 2.2 首版消费与登记契约（首版已实现，长期 API 未冻结）

首版详细范围与 Q1–Q24 确认索引见[生成与消费规格](../../docs/superpowers/specs/2026-11-09-pangumeta-tool-design.md)。基础 TypeId、FieldOptions、TypeInfo/FieldInfo 和 Traits 声明由 Foundation 轻量公共支持承载，不依赖 Clang、Module 或 EngineHost；MetaIR 留在工具内，Pin 与宿主协调位于 Module / EngineHost 层。

`pga::Type` 支持无参数或显式稳定身份，字段默认名称并允许固定 Key；数字 FieldId 不作为必填。Field 采用无参数或一个仅含 Key/ReadOnly 的 Options 参数，所属类型必须标注 Type。孤立 Field、未支持的声明形式明确报错；首版支持可独立包含头中的具名非模板类型及其普通直接成员，不展开基类或复杂成员内部结构。读取返回只读借用，独立写入要求精确值类型及合法赋值；ReadOnly 限制生成写入，不改变原 C++ 字段或整棵对象图。

每个 Meta 输入头在当前图中有唯一 Module 所有者，登记同时接入目标源码，不维护第二份独立人工清单。按头登记消费需求，同头类型共用配置，再按 Module Target 汇总；Game/Editor 共同消费同一库时取需求并集，不保证同一库为每个最终消费者分别保留不同内容。扫描到含 pga 却无明确所有者/输入登记的项目头须报错，其他已登记 Module 的头正常作为依赖；输出集合不在扫描后擅自扩张。

一个 Module Target 首版只采用一套明确 Meta 编译上下文。不能由各业务 TU 的临时宏环境产生不同描述，也不能使用两端有差异的前端版本条件或 `__has_cpp_attribute(pga::...)` 控制反射声明及参数。普通目标平台/架构/配置宏可用；无法还原的 PCH/Modules 上下文拒绝，并以目标编译消费检查支持范围，不外推为任意源码跨前端等价。

请求目标描述时，应用通过生成的已知类型入口提供 EngineHost，取得仅移动、自持正确所属 Module CodePin 的短期借用；无 Service 的 Module 也须支持。借用期间可批量遍历，关闭拒绝新借用，旧借用释放后才可完成卸载；对象实例另由 ScopeBorrow 等保护。Foundation 不反向依赖 Pin/EngineHost。生产入口 `pg::meta::BorrowTypeInfo<T>(host)` 从 `TypeTraits<T>` 取得 Owner 并用 `typeid(T)` 查询，返回 `Result<module::ModuleMetaBorrow>`，不要求应用手拼回调。ModuleId 由公共 C++ 常量唯一定义，Definition 与生成入口共同引用，CMake 只登记符号和声明头。

Attr 与 Clang 库位于同一工具进程，可在解析前注册属性处理器。接口名称中出现 `ParsedAttrInfoRegistry` 或 Plugin 不代表使用动态插件；当前方案不需要 `-fplugin` 或目标 `clang.exe` 的插件符号导出。

必须直接分析原始源码。普通目标 Clang 忽略未知属性后，不能假定其 AST 仍保留可恢复的 `pga` 信息。独立程序也不等于无运行依赖的单文件程序：本次 Windows 工具还依赖 zlib，macOS 工具链接 Clang/LLVM 动态库。

### 2.3 当前生成与消费接口

- pangu_meta_headers 同时登记源码与唯一所有者，EMIT 为 typed_access / description / both / check；pangu_meta_consume 汇总公开头消费并传播生成依赖，不隐式链接业务 DLL。
- pangu_meta_module 只登记 C++ 身份符号、声明头与 Query；pangu_meta_host_catalog 登记 Host 显式 Module 集。Modular 生成唯一原生 Query 包装，Monolithic 生成 pg::module::GeneratedStaticModuleCatalog()；该声明只由登记 Catalog 的 Host 提供定义。
- 每个 Target/Config/OutputRoot 只有一个发布批次：有反射头时 scan 联合发射 Query 槽位，无反射头使用 query，Host 使用 catalog。装配信息留在 Request，不进入类型 IR。
- `TypeTraits<T>::Fields` 提供成员指针访问；语言 IsConst 与政策 IsReadOnly 独立，写能力同时受二者约束。Private 仅描述，不注入 friend。
- 描述启用时生成每头实现及 ModuleMeta.gen.h/.cpp；业务 ModuleDefinition.Metadata 显式引用提供者。PG_META_HAS_DESCRIPTION 由消费并集派生，关闭描述不留下强制数据依赖。
- 当前只支持有对应 CMakeCache 的 Ninja 单配置编译数据库与目标专属 MetaContext.cpp。未知/多配置生成器、不可还原的 PCH/Modules、不透明参数明确拒绝。无法可靠分析的未选择项目内前端条件 include 报 unsupported_condition，不猜测其内容。

POSIX 跨 DSO 的公共元数据值类型须保持默认可见性，ELF 参与 RTTI 查询的宿主还需 ENABLE_EXPORTS；生成器不替用户改变类型可见性。正式人工夹具与生成夹具均按这一受控 ABI 契约验证。

## 3. 警告策略与失败契约

### 3.1 目标编译：受控豁免

已验证的目标侧组合为 `-Wall -Wextra -Wpedantic -Werror -Wno-unknown-attributes`。只对会解析 `pga` 声明的目标及必要消费者设置豁免，不作为所有工程、ThirdParty 的无条件默认值；包含带属性公共头文件的消费者也必须被正确接线。

**豁免作用于整个 `-Wunknown-attributes` 诊断组，并非仅匹配 `pga`。** 其他未知属性、标准属性拼写错误也可能被屏蔽。`-Werror` 继续约束其他已启用警告，但不能单独证明属性正确；不能声称 Meta 只检查有限输入就恢复了全项目所有诊断保障。

### 3.2 Meta 检查：不继承豁免

Attr 移除目标传入的 `-Wno-unknown-attributes`、`-Wno-error=unknown-attributes`，并恢复 `-Werror` 与 `-Werror=unknown-attributes`。正确登记的 `pga` 属性可解析，拼错的属性必须失败。正式参数归一化已覆盖响应文件与等效诊断豁免；不支持的不透明前端输入明确拒绝，不声称支持全部 Driver 参数。

完整构建成功要求 **Meta 检查与目标编译均成功**。参与生成的声明不得漏检；Meta 失败应阻断对应目标，不能用旧生成文件掩盖失败。正式测试覆盖内容不变保持 mtime、逐文件原子替换、失败重试和按 ownership 清理；语义失败不修改旧产物，原生构建仍以失败退出阻断消费者。

驱动参数检查、AST 解析、属性校验任一阶段出现 Error/Fatal，工具都必须失败。Linux 验证曾发现驱动打印错误而 `ClangTool::run()` 返回成功的路径；当前 Attr 额外累计诊断失败状态，不只相信返回值。该修复已完成桌面回归，移动端也纳入负例。

### 3.3 工具与第三方 SDK 分别管理警告

PanguMetaTool 自有代码和生成结果的消费检查使用严格警告策略；第三方 LLVM 库可用 `LLVM_ENABLE_WERROR=OFF` 构建，不影响官方目标 Clang 对引擎使用 `-Werror`。静态分析提示与编译器警告属于不同检查层，不能互相替代通过结论。

## 4. Attribute 命名、分层与参数

### 4.1 已确定：具体 XxxAttribute 与 CamelCase 拼写

**每种属性以具体的 `XxxAttribute` 类型为定义主体；源码拼写只去掉末尾 `Attribute`，保留大写开头的 CamelCase。** `pga` 保持小写，C++ 实现类型仍位于 `pg` 开头的命名空间。

| 具体属性类型 | 源码属性拼写 |
| --- | --- |
| `TypeAttribute` | `pga::Type` |
| `FieldAttribute` | `pga::Field` |

不再以 `PgaAttribute<false>` / `PgaAttribute<true>` 作为长期的属性扩展方式；增加属性应增加独立定义，而不是在一个类中继续增加类别分支。2026-11-07 基础探针的小写 `pga::type` / `pga::field` 仅作为历史验证记录保留，不表示已决定长期支持大小写别名。正式实现统一使用 pga::Type / pga::Field，不兼容历史小写语法。

C# 风格指具体属性类型、去后缀命名和结构化参数的使用体验，不表示 C++ 会自动将属性名绑定到某个类或构造函数。名称映射仍需由 Attr 显式建立；首版已提供 Type/Field 处理器；通用注册 API 未冻结。

### 4.2 区分属性定义、属性值、声明描述与 Clang 处理器

| 概念 | 责任与当前状态 |
| --- | --- |
| `XxxAttribute` 定义 | 描述名称、参数签名、适用声明、重复规则及专属语义；独立具体类型是已确定的组织方向 |
| 某一次属性使用的数据 | 保存解析后的 Id、Options 等值，归属于具体声明与扫描会话；不能存进所有声明共享的注册处理器 |
| `FieldOptions` | 源码可见的轻量 C++ 参数值类型，不依赖 Clang，不是字段描述或属性注册器 |
| `pg::meta::FieldInfo` / `TypeInfo` | 统一基础元数据语义及按需目标描述的接口，不是属性值或注册器；成员布局和具体 API 未冻结，也不自动提供字段读写 |
| `pg::meta::ir::FieldDefinition` / `TypeDefinition` | 对应统一语义的构建期拥有数据表示，附带来源和生成准备信息；正式工具已使用拥有字符串和来源的 MetaIR |
| Clang 适配器 | 提供 `ParsedAttrInfo` 所要求的描述与回调，将前端输入交给对应属性规则 |

当前 Options 探针中的 `FieldAttribute : clang::ParsedAttrInfo` 合并了具体规则与前端接线，实质上是属性处理器。**不要求未来每个 Pangu 属性值类型都直接继承 Clang。** 通过现有 `ParsedAttrInfoRegistry` 接入的对象仍须满足 `ParsedAttrInfo` 接口，但可由内部适配器承担该继承关系。

`ParsedAttr` 是单次属性的解析输入；`Attr` / `InheritableAttr` 是 AST 语义节点；`AnnotateAttr` 是当前保存附加信息的载体；`ASTFrontendAction` / `ASTConsumer` 组织扫描。这些类不是可任意互换的属性注册基类。接口边界见 [ParsedAttrInfo](https://clang.llvm.org/doxygen/ParsedAttrInfo_8h_source.html) 与 [Clang 属性扩展接口](https://clang.llvm.org/docs/ClangPlugins.html)。

### 4.3 大量属性的扩展组织（建议，尚未实现）

建议采用 **具体 Attribute + 声明式参数描述 + 通用绑定器 + 内部 Clang 适配器**。同一属性的拼写、参数顺序/类型、默认值、适用对象和重复规则集中维护，不在注册入口复制第二套字符串。基础参数检查共用，专属规则留在具体属性中；不靠一个大 switch 或一串类型判断处理全部属性。

对外的概念接口是 `Register<FieldAttribute>()`、`Register<TypeAttribute>()`；底层由类似 `ClangAttributeAdapter<T> : ParsedAttrInfo` 的类型接入 Clang。这里是拟议接口，不是已经存在的类或可直接调用的生产 API。具体属性可以是普通类型；是否另设 Pangu `Attribute` 标记基类或虚接口尚未决定，不要求照搬 C# 的运行时继承及逐实例堆分配。

建议按领域维护显式类型清单，启动时集中安装全部处理器，`Scan()` 不再逐项硬编码注册。领域清单只组织工具源码，不增加 Runtime Module、插件 DLL 或另一份构建 Manifest。首版扩展属性通过修改工具源码并重编，新增类型无需改通用扫描器；是否自动生成清单可在实施设计中评估，不重编工具的游戏/Plugin 外部属性扩展属于后续专题，不是本轮待决范围。

安装应发生在首次解析及 Clang 扩展属性实例缓存初始化之前；不能默认支持扫描中动态追加。注册描述可共享，扫描数据必须隔离。Clang 注册表名称和源码 `Spellings` 是两种信息，不能把历史注册键 `pga_field` 当作源码拼写。参考 [Clang 注册实例实现](https://clang.llvm.org/doxygen/ParsedAttrInfo_8cpp_source.html)。

校验分层建议：参数数量/类型/默认值由通用绑定器负责；单属性领域约束由具体属性负责；互斥/重复属性由声明级检查负责；字段 Key / 可选 TypeId 的跨声明与跨 TU 合并或冲突由 MetaIR 汇总检查负责，历史探针数字 FieldId 不因此成为必填协议。生成阶段按需消费强类型信息，不把所有可能属性铺成一个巨型 FieldInfo，也不默认使用字符串到 any 的万能字典。原探针通过不能替代这套重构后的回归。

### 4.4 多位置参数与原生能力边界

**多位置参数表示同一属性括号内各实参按顺序绑定，不是属性在源码中的书写位置。** 例如拟议签名 `Range(minimum, maximum, step?)` 中，第一个数是最小值，第二个数是最大值，第三个可选数是步长。该示例不代表已新增 RangeAttribute。

Clang 的 `NumArgs` 是必需参数数量，`OptArgs` 是额外可选参数数量；普通有限参数定义中，`NumArgs=2, OptArgs=1` 表示允许 2～3 个实参。它们不会自动补默认值、推断参数类型或调用 C++ 构造函数；数量与类型检查、常量读取和默认值绑定仍需实现。[官方接口说明](https://clang.llvm.org/docs/ClangPlugins.html)中的这些能力不等于通用 C# 命名参数系统。

Clang 原生属性可见多种形式，但规则属于各自属性：

| 原生例子 | 参数形式 | 对 pga 的意义 |
| --- | --- | --- |
| `nodiscard` / `nodiscard("reason")` | 无参数或可选字符串 | 自定义属性可声明数量规则，但默认值由工具处理 |
| `gnu::alloc_size(1, 2)` / `gnu::format(printf, 1, 2)` | 可选第二参数；标识符与整数的位置组合 | 不意味任意自定义属性自动支持标识符或函数参数编号语义 |
| `clang::annotate("Meta", 7, true)` | 必需字符串与附加常量表达式 | 是参考实现，不将源码 pga 替换成 annotate |
| `clang::availability(macos, introduced = 10.15)` | 内建属性专用的名称/版本子项 | 有专门 Parser 路径，不能直接复制给任意 pga |

详细规则见 [Clang Attribute Reference](https://clang.llvm.org/docs/AttributeReference.html)；本表不是通用绑定器已经支持全部参数种类的声明。

后续签名设计建议：少量且顺序自然的参数按位置传递；独立语义可组合为不同属性；同一语义的多项可选配置可采用类型化 Options。避免长布尔参数串、重复的等价入口及无边界拆分属性。`Range`、`DisplayName`、`ReadOnly` 等组合只属设计示例，不扩张已确认的首版范围；各属性的正式签名、组合与重复规则仍待确认。

### 4.5 裸命名参数：现有通用路径不直接支持

`[[pga::Field(7, ReadOnly = true)]]` 在没有辅助名称声明时，会在进入处理回调前报 `use of undeclared identifier 'ReadOnly'`。2026-11-08 在 Windows/macOS 的真实自定义属性测试中，添加 `ArgNames` / `NumArgMembers`、设置 `HasCustomParsing=true` 也未改变结果。

通用路径按 C++ 赋值表达式解析参数，不把左侧标识符自动绑定到 FieldAttribute 的成员。事先声明普通可写 bool 得到的是赋值表达式，不能冒充已实现命名绑定；声明 constexpr bool 后再赋值也不成立。目标 Clang 在未知属性豁免下接受相同文本，仅说明忽略成功，不说明 Meta 理解了它。

内建 availability 使用专门的 Parser 分支；`ParsedAttrInfo` 没有通用的原始 token Parser 替换回调，`HasCustomParsing` 不是安装自定义语法函数的开关。依据见 [ParseDecl.cpp](https://clang.llvm.org/doxygen/ParseDecl_8cpp_source.html)及本机 23.1.5 对应源码。

修改 PanguMetaTool 所链接的 Clang Parser，或独立解析属性 token 并关联 AST，都是额外路线，不是本次已验证方案。前者不一定要更换目标编译器，但仍引入 LLVM 源码补丁；后者需解决预处理、宏和声明关联。当前没有为裸命名语法批准这些改造，也不将本次失败扩大为“所有实现方式都不可能”。

### 4.6 类型化 Options：真实 pga 链路已验证

以下示例沿用已验证的参数类型和成员；只展示参数机制，不定义完整 Type/Field 协议：

```cpp
namespace pg::meta
{
struct FieldOptions
{
    bool ReadOnly = false;
    int Priority = 0;
};
} // namespace pg::meta

namespace pg::game
{
struct Actor
{
    [[pga::Field(7, pg::meta::FieldOptions{.ReadOnly = true, .Priority = 2})]] int Value{};
};
} // namespace pg::game
```

外层仍只有 **两个位置参数**：Id 和整个 Options 对象；`.ReadOnly` / `.Priority` 是对象内部的指定成员初始化，不是属性层面的裸命名参数。Options 声明需在 Meta 解析时可见，并保持为不依赖 Clang/工具实现的轻量头文件。不能假设仅定义同名 Attribute 类即可让前端找到 Options。

省略成员时按默认成员初始化求值；指定成员遵守声明顺序，重复、未知成员和窄化按 C++ 规则诊断；本轮严格警告下乱序也被拒绝。与 C# 可自由排列的命名参数不同。语言依据见 [C++ 聚合初始化](https://eel.is/c++draft/dcl.init.aggr)。省略整个 Options 时，探针处理器另行给出 false/0，尚未建立统一默认值来源；不能把这项硬编码当成通用自动绑定。

具体 FieldAttribute 先检查类型，再调用 `Sema::ConstantFoldAttrArgs()`，取得 `ConstantExpr` 的 Struct `APValue`，按 AST 成员名、类型及 field index 读取值。最初直接求 constexpr 类变量得到 LValue；复用 Clang 的属性常量折叠后正确得到对象成员，同时非常量输入继续失败。依据见 [SemaAttr.cpp](https://clang.llvm.org/doxygen/SemaAttr_8cpp_source.html)。不按宿主对象布局 reinterpret_cast，不执行目标程序来取值。

已验证省略/空 Options、部分/全部成员、constexpr 对象、constexpr 函数返回值、类型别名和目标相关常量表达式。真实日志包含 `OPTIONS ... value=Struct ... ReadOnly=true Priority=2`，生成消费者核对 `Id=7;ReadOnly=true;Priority=2`。因此结论已从“内建 annotate 可行”推进到“自定义 pga::Field 可行”。

**已验证的是 bool/int 两成员的专用绑定，不是任意 Attribute 自动绑定系统。** 当前按限定类型名及字段形态识别，正式类型身份、描述协议、构造绑定和 Schema 兼容仍待设计；字符串、数组、嵌套对象、继承、模板依赖参数均未由本轮覆盖。ReadOnly/Priority 的默认值与规则只是测试语义，不改变 C++ const、访问权限或实际 Editor 行为。Options 可作为后续多可选参数候选，不能仅凭 PoC 就冻结所有属性均采用 Options。

### 4.7 身份、Schema 与 Private 边界

基础 PoC 已识别类型限定名、字段名称、规范化字段类型、别名和 Public/Private 访问级别。2026-11-07 的历史临时输入使用 `pga::type("32 字符非零小写十六进制 ID")` 与 `pga::field(非零 uint32 常量)`，支持表达式求值，并拒绝同一 TU 的重复 TypeId 和同一类型中的重复 FieldId。

**这些是验证夹具语法，不是正式 Attribute Taxonomy 或持久化协议。** TypeId 与 ModuleId、ServiceId、C++ RTTI 的用途不同；跨 TU 身份合并、字段演进、重命名、删除后复用策略和 Schema 版本仍需设计。不得将宿主地址、宿主布局、类型名哈希或测试中的目标相关 FieldId 当作已冻结的持久身份规则。

当前 SDK 的含参属性存在性查询需要探针同时登记限定名与短名，再在语义入口严格要求 `pga` 命名空间。裸属性和错误命名空间负例已覆盖，但 `__has_cpp_attribute`、模板、其他语法位置及条件编译的完整行为未覆盖。工具内部前端与目标编译器对属性支持查询可能不同，正式设计必须检查这种差异是否改变被分析源码。

**Private 元数据提取已验证；无侵入自动读写仍是独立研究项。** AST 可见不等于生成函数获得访问权限。本方案不自动注入 friend、不改变访问级别，也不使用偏移量强行访问。消费夹具通过已有公开访问器读取值，不是任意 Private 字段自动访问已经完成。

### 4.8 AST 推导与属性能力边界（设计建议，未冻结）

**C++ 已经表达的事实优先从 Clang AST 推导，引擎额外政策才通过 Attribute 声明。** 类型及其限定、`const`、C++ 访问级别、继承关系、成员/函数签名等，不要求用户再用 `pga` 重复标注；Editor 可见性、序列化用途等不能由语言事实唯一决定的政策才考虑新增 Attribute。构建期推导不能越过 C++ 访问控制，也不能把源类型布局当作持久化格式。

**属性声明不等于能力自动生效。** 每种属性应规定消费者、生成/保留阶段、适用声明、参数与组合规则，以及缺少必要实现时的诊断方式。例如何时使用已有 Getter/Setter、何时生成验证入口，须由对应协议明确；仅标注属性不能保证自动拦截赋值、通知变更或执行验证。无消费者或无合法实现路径的属性不能默认为有效。本节不新增 `pga` 属性名称，也不冻结 Getter/Validator 接口。

## 5. 宿主 SDK 与目标工具链分离

| 层次 | Windows | Linux | macOS |
| --- | --- | --- | --- |
| PanguMetaTool 运行宿主 | Windows 原生 EXE | WSL Ubuntu 原生 ELF | macOS 原生 Mach-O |
| 宿主 C++ 标准库 | MSVC STL | libc++ | libc++ |
| 当前 Clang 依赖接入 | 固定上游源码重建的 MSVC STL 静态依赖闭包 | 同版本上游源码重建的 libc++ 静态依赖闭包 | MacPorts Clang/LLVM 动态库 |
| 当前 Attr RTTI 状态 | 已开启，SDK 以 LLVM_ENABLE_RTTI=ON 构建 | 已开启，依赖库以 `LLVM_ENABLE_RTTI=ON` 构建 | 已开启，无需关闭 Attr RTTI |

### Linux：宿主工具和依赖库均使用 libc++

现成 `/opt/llvm-23.1.0/lib/libclang-cpp.so.23.1` 依赖 libstdc++，已不用于当前有效验证。仅给调用方添加 libc++ 选项不能解决其 C++ 接口不匹配。

有效版本从固定上游提交 `ea7d852a70e8bdfaf601d6626a760f9771b2c4b4`（23.1.0）构建所需 LLVM/Clang 静态库，设置 `LLVM_ENABLE_LIBCXX=ON`、`LLVM_ENABLE_RTTI=ON`、`LLVM_ENABLE_EH=OFF`。PanguMetaTool、Attr、CodeGen 与消费程序均保留 RTTI；动态依赖与符号审计未发现 libstdc++。未重建目标 clang 可执行程序，未覆盖既有安装。

这是显式的依赖准备步骤，不能让普通引擎配置或无变化构建隐式重建 LLVM。正式 Windows/Linux SDK 的现场缓存为 Release/-O3 -DNDEBUG；历史 PoC 曾使用 O1，不据此声明 SDK 分发方案。

### RTTI：正式自有工具全链保留

Windows/Linux 的兼容 SDK 由固定上游提交显式准备，macOS 复用通过预检的 MacPorts SDK。自有工具已移除 Windows PoC 的 -fno-rtti 例外；Sdk/Prepare.cmake 与真实链接/原始 TU 检查验证 RTTI、标准库及版本。普通构建只消费准备好的 PanguMetaSdk.cmake，不隐式构建 LLVM，也不替换目标编译器。

### 移动端：宿主执行、目标解析

| 目标 | 本次运行的 PanguMetaTool | 解析与最终编译上下文 |
| --- | --- | --- |
| Android | Windows EXE，内含固定上游 23.1.0 SDK 前端 | 实际 NDK ABI、API Level、Sysroot、宏与 libc++；最终由 NDK Clang 21.0.0 编译 |
| iOS | macOS 程序，内含 Clang 23.1.2 前端 | iOS ARM64、iPhoneOS SDK、部署版本与 SDK libc++；最终由 MacPorts Clang 编译 |

PanguMetaTool 及其 Clang/LLVM 库不进入 APK/IPA；目标产物只依赖自身 Runtime 和目标标准库。无需在移动设备运行生成器。

正式 Android 复用保留 RTTI 的 Windows 宿主工具；iOS 复用保留 RTTI 的 macOS 宿主工具。宿主 Attr 的 RTTI 设置与移动目标编译配置分开，移动端引擎仍遵守保留 RTTI 的要求。

移动探针从真实 `compile_commands.json` 中取得输入 TU 的命令，去除对象/依赖输出参数，保留目标语义。Meta 的内建资源头匹配工具内部前端；Android 系统与 libc++ 头显式来自目标 NDK，iOS 两阶段使用同一目标 SDK。不能混入宿主 MSVC STL 或宿主默认目标。

宿主前端与目标编译器不再要求插件二进制兼容，但仍需验证语言、内建功能和预定义宏的语义一致性。Clang 23 与 NDK 21 只通过本次输入范围，不构成所有 SDK API 或条件分支的兼容证明。Android API、iOS Deployment Target、设备/模拟器和不同 ABI 应隔离生成上下文；本次只测 iOS Device。

## 6. 生成代码存放、编译与原生构建依赖

**已确定的默认方向：生成代码按源码所属的 Engine / Game Project 分别留在各自的构建树，不因 Game Project 引用 Engine 源码就把 Engine 生成结果写进 Project。** 头文件使用 `.gen.h`、实现使用 `.gen.cpp`；代码目录保留 Public / Private，不设置额外 Source 层；生成实现回到所属 Module/Host Target 编译。原始类型头默认不包含自己的生成头。以下目录与唯一编译归属已由正式生成器及 CMake 接线实现。

借鉴 UE5 的中间目录与生成代码回归所属 Module 编译的组织思路，不照搬其 `.generated.h` 命名、`GENERATED_BODY()`、强制类内注入或手写包含生成实现的要求。不在 Pangu 的 Generated 之外再建立一套 Intermediate 目录体系。

### 6.1 存放位置与命名（已确定）

**PanguMetaTool 生成结果按 Engine / Game Project 源码归属选择构建根，不一律使用当前最顶层工程的 `CMAKE_BINARY_DIR`：**

- **Engine 自身 Module、Engine Plugin**：`<EngineBuildRoot>/Generated/PanguMeta/<Config>/<CMakeTarget>/`。
- **Game Project 自身 Module、Project Plugin、Game/Editor Host Catalog**：`<ProjectBuildRoot>/Generated/PanguMeta/<Config>/<CMakeTarget>/`。

`EngineBuildRoot`、`ProjectBuildRoot` 分别是同一宿主上 Engine 侧和 Project 侧的本地构建根，均遵守既有 Windows/Android 与 Linux/macOS/iOS 构建树规则；结果不写回各自的业务 `Source/`，也不纳入 Git。**即使 Game Project 通过 `add_subdirectory()` 将 Engine 源码纳入同一个 CMake 目标图，Engine 的 PanguMetaTool 产物仍留在 Engine 侧；Project 的产物留在 Project 侧。** 此时顶层 `CMAKE_BINARY_DIR` 通常指向 Project 构建树，不能直接作为两者共用的输出根。

**`CMakeTarget` 指直接编译该生成实现的实际 CMake 目标名**：Module 元数据使用其唯一库目标名，Host Catalog 使用对应宿主目标名。这里不是 UE/UBT 的应用级 Target，也不是平台 Target Triple、生成任务的辅助目标或逻辑 Module 名；不使用 `Pangu::Foundation` 这样的 Alias 作为目录标识。Module 名与库目标名可以不同，目录以实际构建归属为准。

| 逻辑归属 | 实际 CMake 目标名 / 目录名 | 对外 Alias |
| --- | --- | --- |
| Foundation Module | `PanguFoundation` | `Pangu::Foundation` |
| Module 管理模块 | `PanguModule` | `Pangu::Module` |
| EngineHost Module | `PanguEngineHost` | `Pangu::EngineHost` |
| Game Host（独立 Project 测试夹具） | `MyGame` | 无 |
| Editor Host（同一测试夹具） | `MyEditor` | 无 |

上述名称对应现有 `Source/Runtime/{Foundation,Module,EngineHost}/CMakeLists.txt` 与 `Tests/CMake/SourcePluginProject/MyGame/CMakeLists.txt` 的目标定义，这些目标已接入 PanguMetaTool Query/Catalog 登记。下面的 `MyGameGameplay` 则是 **Game Project 自有 Module 的假设库目标名**，不是已核实的项目目标。

```text
# Engine 侧（Windows 示例，不在 MyGame 的构建树中）
D:/Git/PanguEngine/Build/Windows/Generated/PanguMeta/Debug/
├── PanguFoundation/
│   ├── Public/
│   │   └── Example.gen.h
│   ├── Private/
│   │   ├── Example.gen.cpp
│   │   └── ModuleQuery.gen.cpp
│   ├── Meta.d
│   └── Meta.stamp
├── PanguModule/
│   └── Private/
│       └── ModuleQuery.gen.cpp
└── PanguEngineHost/
    └── Private/
        └── ModuleQuery.gen.cpp

# Game Project 侧（与 Engine 并列的另一棵构建树）
D:/Git/MyGame/Build/Windows/Generated/PanguMeta/Debug/
├── MyGameGameplay/
│   ├── Public/
│   │   └── Player.gen.h
│   ├── Private/
│   │   ├── Player.gen.cpp
│   │   └── ModuleQuery.gen.cpp
│   ├── Meta.d
│   └── Meta.stamp
├── MyGame/
│   └── Private/
│       └── StaticModuleCatalog.gen.cpp
└── MyEditor/
    └── Private/
        └── StaticModuleCatalog.gen.cpp
```

目录示例中的 `Example`、`Player` 等文件仅为产物示意，不声明 Foundation 或 MyGame 已有这些反射输入。`.gen.h` 仅在确有接口声明需求时生成；`ModuleQuery.gen.cpp`、`StaticModuleCatalog.gen.cpp` 是职责示例名；Meta.d / Meta.stamp / Meta.outputs.json 是每个已启用任务的依赖、成功标记与拥有清单；无任务目标不生成。取消的是**生成树中的 Source 层**，不是删除项目已有的 `Source/` 目录。

**源代码所有者决定哪棵构建树，实际 `CMakeTarget` 决定树内子目录。** 同一 Engine 工作区对应一个当前 Game Project；切换 Project 不改变 Engine 生成代码的存放归属，仍复用 Engine 侧目录，Project 自有代码及 Host Catalog 留在各自 Project 构建根。生成源码仍回归所属 Module/Host 目标编译。

平台、架构/ABI、SDK/设备类型遵守既有 Engine / Project 构建根规则；配置由 Config 区分，树内由实际 `CMakeTarget` 名标识。**切换 Game Project 时重新 Configure，并按当前 Project 的真实编译配置更新 Engine 共用位置的派生请求与输出清单，按需覆盖之前的生成及编译产物。** 同一 EngineBuildRoot 的 Configure/build/test 会话串行；直接构建旧 Project 时由原生依赖识别切换、触发重配后恢复，无法恢复则失败，不能沿用旧成功标记。相关输入、Project 来源、宏、SDK/ABI 或模式变化应正确失效；同内容按差异写入和原生编译依赖复用，不要求无条件 Clean，也不增加每次 no-op 的 Meta 工具检查。首版同时落实 Engine 对象、库、导出头和 Meta 产物实际归属 EngineBuildRoot；实际分根已实现；正式回归同时覆盖 A=17/B=17 的同参数 A→B→直接 A，以及 A=17/B=29 的不同宏往返、失败恢复与 Debug/Release 请求失效。产物文件名不嵌入平台名。

输出映射须保留输入头文件的必要相对路径信息，不能只取 basename；不同输入根或子目录的同名头不得相互覆盖。上述树为简化示例，正式映射还须明确源根区分和保留文件名冲突规则，不默认用机器绝对路径或不透明长哈希命名。原始头在 Public 不意味着其 `.gen.cpp` 也在 Public。

### 6.2 Public / Private 与唯一编译归属（已确定）

| 产物 | 放置位置 | 使用与编译归属 |
| --- | --- | --- |
| 对其他 Module/Host 公开的生成声明、Traits 或模板接口 | `Public/**/*.gen.h` | 按公开 API 需要传播包含路径与生成依赖 |
| 仅所属目标使用的生成声明 | `Private/**/*.gen.h` | 不向消费者公开 |
| 类型/字段等生成实现 | `Private/**/*.gen.cpp` | 作为 PRIVATE 源文件加入所属 Module 的唯一库 Target |
| Module 内部汇总、Query 所需生成适配 | Module 的 `Private/` | 编译进原 Module，不另建 Meta 库 |
| Host 静态装配 Catalog | Host 的 `Private/` | 编译进对应 Game/Editor Host，不塞进 Module 或全局公共 Catalog |

**所有生成 `.cpp` 都是内部实现。** 不为 Module 另建独立 Meta 库或全局生成实现库；生成代码使用所属目标的宏、包含目录、导出设置、RTTI/异常和警告策略，并由该目标平台的编译器编译。宿主 PanguMetaTool 与 LLVM/Clang 依赖不进入目标库。

声明须有明确拥有者。同一个头被多个消费者包含，不因此为每个消费者重复生成和链接实现；同一个 Module Target 供多个 Host 复用时汇总消费需求，不按 Host 无条件重复其元数据，也不承诺最终程序自动裁剪全部未用描述。首版每目标只采用一套 Meta 上下文，语义配置改变时按当前 Project/配置重新生成。Host 各自的装配集合仍由既有 Module/Host 契约决定。

实现阶段要验证显式引用链、链接裁剪和静态归档提取，不能仅因源文件编进库就假定注册内容必然进入最终程序；不依赖全局静态构造补救。Export.h 仍由 CMake 生成；ModuleQuery.gen.cpp 已迁入 Meta 唯一批次，不再保留旧 Query 生产者。

### 6.3 原始声明与生成头的依赖方向（已确定）

依赖方向为：原始 `Actor.h` 先被 PanguMetaTool 分析；需要目标实现时，`Private/Actor.gen.cpp` 再包含原始头并编译。**原始类型头默认不包含自己的 `Actor.gen.h`，业务 `.cpp` 也不默认手动包含 `.gen.cpp`。** 仅需模板/强类型访问时可只输出 `.gen.h`，仅需非内联实现时可只输出 `.gen.cpp`；没有目标消费者的数据可仅保留在 MetaIR，不要求全部转成 C++。

只有确实需要调用点可见的模板、编译期 Traits、内联实现或生成 API 声明，才提供伴随 `.gen.h`。由需要这些接口的消费者按需包含，生成头可按需要包含原始类型头。Public/Private 根据生成接口的可见性划分，不机械复制输入文件分类；不能形成“先包含自身输出，才能完成首次 Meta 分析”的循环。

公开生成头的首次构建顺序与依赖传播需要明确接线，不能只添加 include 路径；消费者不得越过 PRIVATE 依赖读取内部生成头。Private 字段的 C++ 访问控制不因生成代码与原类型处在同一 Module 而改变，无侵入访问仍是独立研究项。

### 6.4 文件、生成任务与编译粒度

默认按输入头组织所需的 `.gen.h` / `.gen.cpp`，由已登记的消费需求确定固定输出槽位，并按需提供小型 Module 汇总代码；一个头内新增或删除类型，尽量不改变选中的生成源文件集合。不是每个类型都必须产生 Runtime 描述数组，也不要求每份 IR 都落盘。生成文件粒度不等于 Clang 解析任务粒度，也不等于最终编译单元粒度。

**首版已按 Module/目标上下文批处理，每个 Target/Config/OutputRoot 只有一个生产者；原生依赖与增量行为已通过正式回归，性能数据见验收基线。** 一次调用可写多个文件，但输入变化可能使该任务重解析一组头。按头拆输出只帮助限制重写与后续编译范围，不证明 AST 分析已增量化。

生成 `.cpp` 默认由 CMake 登记并独立参与目标编译；不要求用户手写包含生成实现，也不同时包含并单独编译同一份 `.gen.cpp`。Unity/PCH、聚合编译或更细的扫描粒度等优化在测量后再决定，不在首版自制持久 AST 缓存或另一套调度器。

首版生成输入已确定为可独立包含的头文件，仅在业务 `.cpp` 内定义的反射类型不在支持范围；不通过包含整个业务 `.cpp` 隐式改变编译归属。具名非模板类型及普通成员等详细支持边界见首版规格 §3.3。

### 6.5 原生依赖接线与增量验收（首版已实现，长期 API 未冻结）

沿用 CMake 4.0+ / Ninja 原生图，按真实消费者显式调用 PanguMetaTool。工具独立按宿主构建，SDK 准备与日常引擎构建分开；不扫描全部仓库、不引入逐 TU 收据、自制增量调度器或第二份手工源码 Manifest。

生成输入与输出应由已有目标登记和明确的 Meta 输入归属推导；新增/删除输入需要正确更新原生图，不能依赖生成完成后对输出目录 GLOB 来发现未知 `.cpp`。传给工具的参数文件是构建图派生输入，不是另一份人工维护的权威清单。**在 Project 顶层构建图中，也应按源码所有者明确选择 Engine / Project 两种生成根；不能直接对所有输出使用顶层 `CMAKE_BINARY_DIR`。** 每个输出只有一个生产者，多个消费者依赖同一生成任务，不并行写同一文件；Engine 侧由当前 Project 的构建图消费，同一 EngineBuildRoot 遵守串行会话与 Project 切换规则。外部输出路径、依赖传播、增量恢复及 A→B→A 切换必须通过 CMake/Ninja 实际验收。

生成必须先于其消费者编译；不使用 POST_BUILD 生成本目标马上需要的源码，也不在每次 Configure 无条件执行全量 Meta。公开生成头与生成实现都要纳入正确的文件及目标依赖。

当前接线以 `Meta.stamp` 为 `OUTPUT`，代码文件登记为 `BYPRODUCTS`，`DEPFILE` 记录 Meta 读取依赖。成功标记只在任务完整成功后更新，实际代码内容不变时不重写；`Meta.stamp` 不是逐 TU 收据或自制缓存。丢失产物恢复已有正式回归；首版限定单配置 Ninja，PCH/C++ Modules 输入与多配置仍不在支持范围。

Meta Depfile 决定何时重新分析/生成；正常 C++ 编译依赖决定何时重编 `.gen.cpp`。即使生成内容不变，若其包含的原始头变化，生成实现仍可能需要重编。解析参数、工具及其配置版本变化必须能使相应任务失效；删除/重命名输入后的拥有文件清理和失败恢复已有正式回归；取消不会获得新的成功 stamp。

| 场景 | 首版正式集成覆盖的行为 |
| --- | --- |
| 不影响 Meta 输入的普通 `.cpp` 函数体变化 | 正常编译，不运行无关 Meta 任务 |
| Meta 输入头或传递依赖变化 | 运行对应生成任务，按内容差异更新输出 |
| 宏、工具、SDK 或配置变化 | 正确失效，不混用旧目标语义 |
| 切换 Project 并切回 | 重新 Configure 后在同一 Engine 生成目录更新当前请求，始终消费当前 Project 的结果，旧成功标记不能掩盖切换 |
| 直接构建旧 Project | 原生依赖触发重配恢复，或明确失败；不静默混用当前 Engine 产物 |
| 生成文件被删除 | 原生图补生成，不依赖手工 Clean |
| Meta 错误、失败或取消 | 阻断对应消费者，旧结果不冒充本轮成功 |
| 无变化 | 不启动 PanguMetaTool，不重新编译或链接 |

### 6.6 现有实现与迁移边界

正式 CMake 生产管线已覆盖 OUTPUT/DEPENDS、每头实现、PUBLIC 传递依赖、内容不变 mtime、原生 no-op、丢失产物及失败恢复；移动夹具使用同一生成协议与真实目标参数。历史 Python 适配脚本不再是正式验收入口。

现有探针的 `Generated.h` 是历史产物名，本次不重命名探针或迁移 BuildRoot。`.gen.h` / `.gen.cpp`、Public/Private 与 Engine/Project 分根已落实。两根选择、共享切换、Depfile、扫描归属、跨 TU 去重、冲突映射及正式登记 API 均已实现；多配置生成器仍明确拒绝。Export 保持 CMake 生成，Query/Catalog 已迁移至 Meta。

## 7. Module / Service 与 Schema 的边界

已实现的 Module/Catalog 自动生成遵守当前 [Module API](module-api.md)、[Plugin / Module](plugin-module.md) 与 [ADR-0015](../decisions/0015-unified-link-modes-module-manager.md)：Monolithic 使用各 Host 显式静态 Catalog，Modular 使用各 Module Query；不恢复历史混合链接模式或在 SHARED 中内嵌另一个逻辑 STATIC Module。

Module 生成代码附着到其唯一 Library Target，Host Catalog 属于对应最终消费目标，不建立第二个 Module Library。Module 身份/运行期依赖与 CMake 编译链接依赖不混同，不新增 Module.toml，不由 Plugin 发现阶段生成全局 Catalog。

生成器不接管声明事务、Scope、CodePin 或卸载；不依赖全局静态构造自动注册，不引入 BinaryImage、ExtensionDescriptor 或万能 ExtensionManager。Concrete / Contract / Registry Service 三模式保持不变，不要求所有 Concrete Service 具有反射描述，也不强制新增 `pga::Service`。

统一元数据语义由 `pg::meta` 定义，MetaIR 在 `pg::meta::ir` 中扩展构建期信息；编译期已知类型优先强类型生成，只有需要动态枚举或 Registry Contribution 时才携带动态描述。同一模型可供后续 Reflection、Serialization、Save/Load、Networking、Inspector 等领域按需复用，但领域协议、磁盘格式、函数签名、版本演进和 Runtime 注册撤销 API 尚未冻结。不能取消必要的运行时类型、所有权或外部输入校验。

### 7.1 纯 C++ 定位与反射消费边界（设计建议，未冻结）

以下是后续 Reflection / Attribute Taxonomy 的设计方向，**不扩充首版 Module/Catalog 与 Type/Field 目标，不代表 Runtime Meta、Editor Inspector 或字段访问 API 已经实现**：

1. **纯 C++23，不为脚本体系预留强制成本**：当前项目只有原生 C++，没有 Blueprint、Lua 等运行时脚本系统。反射不以脚本 VM、动态脚本类、脚本函数覆写或任意函数通用 `Invoke` 为目标；如未来引入脚本，再独立设计绑定和执行协议。C++ Module 的动态加载不等同于动态脚本类型。
2. **统一属性 / Schema，不统一 Object 体系**：普通值类型、ECS Component、Event/Message、Asset 数据及业务 Service 可以复用 Type/Field 描述，但不因使用 `pga` 就必须继承通用 `Object`、接受统一 GC、携带每实例反射状态或进入全局对象 Registry。实例身份、存储、Owner、Handle 和销毁仍由所属领域决定；某领域确需基类时单独设计，不作为反射前置条件。
3. **强类型生成优先，动态反射按需**：PanguMetaTool 在 Build 阶段分析并生成可由 C++ 编译器优化的类型化访问或操作；确有需要时再提供编译期可用描述。Editor 中类型未知的 Inspector、资源选择或诊断工具可消费动态类型/字段描述，Game Runtime 只保留实际功能需要的动态信息；普通 C++ 与 ECS 热路径不默认经过按名查找或通用字段分派。声明 `pga::Type` 不自动意味着全局 Runtime 注册。
4. **Build / Editor / Runtime 是消费和保留边界**：所有参与生成的 `pga` 声明统一由 Build 阶段严格校验、规范化；属性处理器与 Clang AST 不进入目标 Runtime。Editor 可保留展示元数据、通用检查和受控编辑操作；Game Runtime 按序列化、数据恢复及其他真实消费者保留必要代码与协议数据，避免携带无用编辑信息。Editor 自身运行时使用动态反射，不等于 C++ 编译期反射；三层也不要求三套 Attribute 基类或额外 Module 库。
5. **字段描述与写入协议分离**：能枚举 Field 或取得布局，不等于能合法读写 private 字段、验证业务不变量或观察全部 C++ 赋值。生成的访问操作必须符合 C++ 访问控制，并按需求通过明确的 Getter/Setter、验证、通知及 Undo/Redo 入口；直接字段访问和领域专用恢复路径有各自契约，不能因属性声明隐式获得线程安全或自动拦截赋值能力。动态描述、操作入口和消费者缓存还必须遵守现有实例 Borrow/Lease、Module 注册撤销及 CodePin 驻留顺序。

**展示提示、数据验证和业务不变量必须区分。** 例如 Inspector 的滑条范围只约束交互提示，不保证普通 C++ 赋值合法；资源导入或存档加载时的字段范围验证属于受控数据入口；跨字段关系等业务不变量仍由领域类型与操作维护。某个消费者执行了范围裁剪，也不能宣称所有写入路径均被约束。暂不冻结对应属性名称和参数协议。

具体 TypeInfo/FieldInfo、编译期描述、动态 Registry、字段访问签名及跨领域 Schema 协议仍待分别设计，不将这些建议提升为 Frozen 接口。

## 8. 已完成的验证矩阵（2026-11-07～08）

以下是**隔离 PoC 的历史验证记录**，不是正式 `Tests/`、全引擎 CI 或发布验收。归档时核对了各 BuildRoot 的 `verification-summary.json`；不因本文更新而宣称重新执行过全部构建。版本、路径及最低 OS/API 值只描述测试环境。

2026-11-09 首版规格的 Android 工作与验收已限定为 ARM64（arm64-v8a），不开展其他 Android ABI 的新增实现或测试。下面的四 ABI 记录仅保留其历史证据，不能转化为本轮任务；首版完整矩阵见规格 §8。

| 目标 | 实际环境 | 已完成范围 | 结果 |
| --- | --- | --- | --- |
| Windows x86_64 | 官方 Clang/SDK 23.1.5 + MSVC STL | 独立工具、生成、目标编译链接和消费运行 | 13/13 CTest；共享 Attr 修复后重新回归 |
| Linux x86_64 | WSL Ubuntu；Clang 23.1.0；重建 libc++ SDK | 独立工具与消费运行；无 libstdc++ 依赖审计 | 14/14 CTest |
| macOS x86_64 | MacPorts Clang 23.1.2 + libc++ | 独立工具与消费运行 | 14/14 CTest |
| Android arm64-v8a | NDK 30.0.16248370 / Clang 21.0.0 / API 29 / c++_shared | 宿主 Meta 检查、交叉编译链接与 ELF 检查 | 18/18 |
| Android armeabi-v7a | 同上，32 位 ARM | 同上 | 18/18 |
| Android x86 | 同上，32 位 x86 | 同上 | 18/18 |
| Android x86_64 | 同上，64 位 x86 | 同上 | 18/18 |
| iOS Device ARM64 | MacPorts Clang 23.1.2 / iPhoneOS 18.2 SDK / minOS 18.0 | 宿主 Meta 检查、交叉编译链接与 Mach-O 检查 | 18/18 |

移动端合计 90 项**宿主侧**检查，不是 90 项设备运行测试。没有把 Android/iOS 产物当宿主可执行程序运行。

### 8.1 检查内容

桌面正例通过标准库头、目标宏和类型别名验证上下文，生成一个类型两个字段；消费程序静态断言并实际运行。十二个属性负例检查拼写、错误位置、参数类型/数量、零或重复 FieldId、非法或重复 TypeId、缺少标记 Owner、其他未知属性、缺失或错误命名空间。负例必须同时满足预期退出码、诊断与无输出，不能把崩溃当作通过。

Linux/macOS 增加 Driver 错误传播测试。移动每组包括十二个属性负例、Driver 错误传播、目标严格拒绝未知属性、豁免后通过、普通警告仍报错、目标二进制检查和相同内容生成稳定性；完整脚本另核对生成构建和 no-op。

所有有效平台记录均保留严格 Meta 检查、受控目标豁免、相同内容不重写及无变化 Ninja。三个桌面夹具的生成文件 SHA256 相同：`829b392c4bf8907ece70f2cd4f3c720fbbf872bc200461165cfd0208e21b889e`；不将这一结论外推为所有目标元数据必须相同。

### 8.2 移动端目标语义与依赖证明

移动夹具让 FieldId 表达式引用目标信息，由最终目标编译器静态断言交叉核对：`sizeof(void*) * 8` 在 Android 32 位得到 32，在 Android/iOS 64 位得到 64；Android 字段得到 `1000 + API29 = 1029`；iOS 字段得到 `1000000 + 180000 = 1180000`。这些是检测错用宿主配置的测试哨兵，不是持久 FieldId 分配建议。

Android 四个 ELF 依赖 `libc++_shared.so` 及 Android 系统库；iOS 动态库的 `LC_BUILD_VERSION` 为 iOS / minOS 18.0 / SDK 18.2，依赖系统 libc++、libSystem。未发现 LLVM/Clang、libstdc++ 或 MacPorts 宿主路径进入移动产物。测试 dylib/so 的形式不自动成为正式 App 打包或移动 Module 加载政策。

### 8.3 多位置参数与裸命名参数（2026-11-08）

Windows Clang 23.1.5 与 macOS Clang 23.1.2 分别完成 17 个 Attr 场景及 2 个目标编译对照；各有 6 个正向场景完成生成、严格编译及消费运行。多位置参数与处理器默认值通过；裸 `ReadOnly = true` 在普通配置、ArgNames 配置、HasCustomParsing 配置中均未获得命名绑定，未声明名称时回调没有进入、工具失败且无输出。

这轮完整配置对照仅测 Windows/macOS，不将“所有预期结果符合”表述为“命名参数支持成功”。后续 Options 矩阵也保留裸命名参数拒绝负例，但不等于重跑了全部三种配置对照。

### 8.4 真实 pga Options 参数（2026-11-08）

| 宿主工具 / 消费目标 | 解析与消费环境 | 脚本结果 | 正例消费范围 |
| --- | --- | --- | --- |
| Windows / Windows x64 | Clang 23.1.5 + MSVC STL | 27/27 | 9 个程序编译、链接、执行 |
| Linux / Linux x64 | Clang 23.1.0 + 既有全 libc++ SDK | 27/27 | 9 个程序编译、链接、执行 |
| macOS / macOS x64 | MacPorts Clang 23.1.2 + libc++ | 27/27 | 9 个程序编译、链接、执行 |
| Windows / Android 四 ABI | 宿主前端 23.1.5；NDK 30.0.16248370 / Clang 21.0.0 / API29 | 每 ABI 27/27 | 每 ABI 9 个 ELF 库交叉编译、链接及检查 |
| macOS / iOS Device ARM64 | MacPorts 23.1.2 / SDK18.2 / minOS18.0 | 27/27 | 9 个 Mach-O 库交叉编译、链接及检查 |

八组共 **216 项脚本检查，不是 CTest 计数**；每组为 9 个正例、16 个负例和 2 个警告策略对照。72 个正例均生成并编译链接，其中 27 个桌面程序实际执行，45 个移动库没有设备运行。Android 四 ABI 为 arm64-v8a、armeabi-v7a、x86、x86_64。

正例覆盖 4.6 节列出的对象、默认值和常量求值；负例覆盖裸名称、未知/重复/乱序成员、类型错误、窄化、错误 Options 类型、用 bool 冒充 Options、运行时变量/函数、领域约束、参数数量/身份和属性拼写/命名空间。失败要求诊断、exit=1 和无新输出。生成消费者 static_assert 对照 payload，重复生成保持字节与时间戳不变。

`Priority=sizeof(void*)*8` 在 32 位目标得到 32、64 位得到 64，再由目标编译器核对；这是目标上下文哨兵，不是 Priority 业务规则。移动产物检查仍使用目标 libc++，未将 LLVM/Clang/宿主 C++ 库带入目标。Linux 只读复用已构建的 libc++ SDK；Windows 沿用无 RTTI SDK 的临时适配，未解决第 5 节的生产接入问题。

Windows 两个追加探针的格式检查通过；clang-tidy 分别为 0 错误 / 33 条提示（裸命名参数探针）、0 错误 / 28 条提示（Options 探针）。这些是 2026-11-08 留存结果，不把非阻断提示忽略成 lint-clean，也不声称本次文档更新重跑了八组验证。

## 9. 尚未实现或验收的范围

### 9.1 后续专题需要解决的问题（未冻结）

- **稳定身份和 Schema 演进**：TypeId / FieldId 如何跨重命名保持身份、如何处理删除后重用与兼容迁移、如何校验跨 TU/Module 唯一性；不使用当前 PoC 的目标相关常量或内存布局作为持久化协议。
- **Editor Inspector 最小消费契约**：类型/字段枚举、默认值与实例的编辑上下文、可见与可写策略、Getter/Setter、验证和 Undo/Redo 如何组织；明确通用 Inspector 与业务定制化的接口和实例借用边界。
- **性能与成本验收**：分别测量首次/增量/无变化构建、生成文件及二进制体积、元数据内存与启动注册成本，以及直接 C++ 访问、强类型生成操作、动态字段查询/访问的开销；既评估运行时成本，也防止 Meta 引入显著构建回退。

以上仅列出需要专题设计与验收的问题，不冻结新的属性名、Runtime Registry、Schema 布局或性能阈值，也不扩充首版工作范围。

### 9.2 当前限制与未交付范围

- 首版生产工具、CLI、MetaIR 生命周期隔离、Type/Field 常量绑定、生成与发布、Query/Catalog 和描述 Pin 已实现；Public C++ API/ABI、通用属性绑定器、复杂属性值和不重编工具的外部扩展未冻结。
- 具名非模板类型的普通直接成员属于首版范围；模板、继承展开、位域、引用、static、union 等不支持形式明确诊断。跨 TU 合并与冲突已检查，不承诺任意前端条件/宏下的完全等价。
- 只支持 CMake Ninja 单配置数据库；PCH 只接受可还原原始头的形式，二进制 PCH/C++ Modules、多配置生成器和任意外部手工数据库不支持。Unicode、别名、Depfile、发布失败与旧产物恢复均有正式回归。
- Private 无侵入读写、完整 Runtime Reflection/Serialization/ECS/资产系统、宿主 SDK 安装包、移动签名/安装/启动、APK/IPA、真机/模拟器、iOS Simulator 与 Apple Silicon 宿主不属于本轮覆盖。
- 自有工具全链保留 RTTI；仍有已披露的既有/第三方静态分析提示，不声明全工程 lint-clean。性能基线只对应实际输入、机器、优化与模式，不外推大型工程或跨机器保证。

实际平台覆盖、失败修正、静态分析边界、性能与恢复状态见[正式验收](../../docs/superpowers/plans/2026-11-09-pangumeta-tool-validation.md)。第 8 节和下方原型路径是历史证据，不替代长期 Tests。
## 10. 证据归档与复验边界

基础原型位于 `.probe/pangumeta-standalone/`，追加参数探针位于 `.probe/pangumeta-named-arguments/` 与 `.probe/pangumeta-options-arguments/`，均被 Git 忽略。这些是非 Plan PoC，不将未版本化目录作为规范成立或新检出后复现的前提；本文与 ADR 保存长期结论，正式集成已把首版契约落入 Tests，历史探针无需参与运行。

源码定位：基础探针 `Attr/Scan.cpp` 提供属性回调、Collector 和扫描入口，`Attr/Scan.h` 保存 Field/Type/ScanResult，`CodeGen.cpp` 消费模型；追加两个探针各自的 `Probe.cpp` 包含具体 FieldAttribute，Options 参数声明位于 `Fixtures/FieldOptions.h`。这些历史实现并存用于验证不同问题；生产 BuildTools/PanguMetaTool 独立实现首版契约，不把原型目录当作依赖，也未承诺通用注册框架。

| 原型入口 | 本机产物与证据位置 |
| --- | --- |
| `Verify.ps1`、根 README | `Build/Windows/PanguMetaStandalone/` |
| `Posix/Verify.py`、`Posix/README.md` | macOS：`~/PanguEngineBuild/macOS/PanguMetaStandalone/` |
| `LinuxLibcxx/Build.py`、`Audit.py`、README | Linux：`~/PanguEngineBuild/Linux/PanguMetaLibcxx/Build/tools/pangumetaprobe/` |
| `Mobile/Verify.py`、`Mobile/README.md` | Android：`Build/Android/PanguMetaMobile/<ABI>/`；iOS：`~/PanguEngineBuild/iOS/PanguMetaMobile/arm64/` |
| `.probe/pangumeta-named-arguments/Verify.py`、README | Windows：`Build/Windows/PanguMetaNamedArguments/`；macOS：`~/PanguEngineBuild/macOS/PanguMetaNamedArguments/` |
| `.probe/pangumeta-options-arguments/Verify.py`、README | Windows：`Build/Windows/PanguMetaOptionsArguments/`；Linux/macOS：`~/PanguEngineBuild/<平台>/PanguMetaOptionsArguments/`；Android：`Build/Android/PanguMetaOptionsArguments/<ABI>/`；iOS：`~/PanguEngineBuild/iOS/PanguMetaOptionsArguments/arm64/` |

各目录保存生成文件、测试和诊断日志、`verification-summary.json`；Linux 还保存 `libcxx-audit.json`，移动端保存真实编译数据库与目标参数。Linux 旧 `PanguMetaStandalone/` 目录仅为已替代的 libstdc++ 历史结果，不能充当当前全 libc++ 方案的证据。

复验需要对应的宿主工具、LLVM 开发库及资源头、SDK/NDK 和已配置 BuildRoot；已有脚本不代表可分发的正式安装流程。原型 C++ 直接读取共享源码，构建产物按宿主文件系统隔离，遵守 [构建系统](../foundation/build-system.md)。本次归档不迁移或提交探针，不携带大体积 LLVM 源码和构建产物。

## 关联文档

[工具链](../foundation/toolchain.md)、[构建系统](../foundation/build-system.md)、[C++ 规范](../foundation/cpp-guidelines.md)、[Android 工具链](../foundation/android-toolchain.md)、[Apple 工具链](../foundation/apple-toolchain.md)、[Plugin / Module](plugin-module.md)、[Module API](module-api.md)、[Service](service.md)、[ABI](../foundation/abi.md)、[ADR-0017](../decisions/0017-standalone-pangu-meta.md)。
