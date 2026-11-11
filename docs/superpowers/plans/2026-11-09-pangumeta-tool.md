# PanguMetaTool 首版生成与消费闭环 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 交付实际 Engine/Project 产物分根、独立 PanguMetaTool、MetaIR、按需强类型生成、受保护描述借用及 Query/Catalog 原生增量生成闭环。

**Architecture:** CMake 接线仍处于一个原生目标图，Engine 自有目标使用共用 EngineBuildRoot，切换由原生配置/编译依赖恢复。宿主工具通过 Attr → 拥有数据的 MetaIR → CodeGen → 批次发布运行；目标侧 Foundation 仅提供轻量语义，Module/EngineHost 负责已接纳身份与 Pin。ModuleDefinition 保留原业务定义及 Register 回调，新增可选的只读元数据提供者接线，不引入全局 Registry。

**Tech Stack:** C++23、Clang 23.x 宿主前端/LibTooling、RTTI、无 C++ Exception、CMake 4.0+、Ninja、CTest/GoogleTest、现有 nlohmann JSON；目标工具链继续按平台规范。

**Spec:** [PanguMetaTool 首版生成与消费闭环](../specs/2026-11-09-pangumeta-tool-design.md)，规格与本轮输入基线 `46e237c`；同时阅读该规格与 AGENTS.md、Tests/README.md。Tasks 1–13、独立任务评审、全分支评审及最终修复复审均已完成。正式验收、被测版本与剩余改进见同目录 validation 文档。

## Global Constraints

- 一个 Pangu Module 一个真实 STATIC/SHARED 库，同图统一 Monolithic/Modular；保留 C++ ModuleDefinition 与 Register 回调，不生成整个 Definition，不建立第二套运行期依赖图。
- 正式工具及 CMake Target 为 `PanguMetaTool`，源码根 `BuildTools/PanguMetaTool/`；生成根 `Generated/PanguMeta/<Config>/<CMakeTarget>/`，代码目录 Public/Private，后缀 `.gen.h` / `.gen.cpp`，产物名不包含平台名。
- `pg::meta` 定义统一基础语义及按需目标接口；`pg::meta::ir` 拥有构建期数据。IR 不保存 Clang AST 借用，首版不新增 Meta 文件、Exporter 或磁盘 Schema。
- Type 可无参数、稳定 TypeId 可选；Field 必须有 Type 所属声明，默认 Key 为字段名，Options 仅 Key/ReadOnly，数字 FieldId 不是必填。
- Foundation 轻量支持不依赖 Clang、Module 或 EngineHost；代码保护及宿主协调位于上层。目标描述由目标编译器构造，不复制宿主对象内存/地址/布局。
- 每头唯一 Module 所有者；登记同时接入目标源码；按头选择消费需求、按 Module 汇总；每 Module 一套明确上下文。未知所有者/漏登记 pga 项目头报错。
- 一个 Engine 工作区对应一个当前 Game Project；实际 Engine 对象、库、导出头和 Meta 归 EngineBuildRoot。切换由原生重配/编译依赖恢复，同一 BuildRoot 会话串行，不增加 Project 变体目录。
- 描述借用仅移动、自持正确 Module Pin；无 Service 的 Module 同样支持；关闭拒绝新借用，旧借用阻止卸载完成。CodePin 不替代实例 ScopeBorrow。
- 启用任何工具任务（包括仅 Query/Catalog）都要求完整宿主工具和匹配 SDK；开发库显式准备，不隐式构建 LLVM；Windows/MSVC STL，Linux/macOS/libc++，自有工具保留 RTTI。
- 本轮 Android 只开展 ARM64（arm64-v8a）工作与验收；iOS 为 Device ARM64。三桌面实际运行/双模式，移动只编译、最终链接和宿主产物检查，不运行交叉 GTest。
- Windows/Android 为 `Build/Windows` / `Build/Android`；Linux 为 `$HOME/PanguEngineBuild/Linux`，源码 `/home/moozen/WindowsGit/PanguEngine`；Mac 为 `$HOME/PanguEngineBuild/macOS` / `iOS`，源码 `/Users/moozen/WindowsGit/PanguEngine`，通过 `ssh -o BatchMode=yes moozen-macos`。不复制 Engine 源码到临时目录。
- 本轮生产接线与验收使用根 Ninja 单配置 presets；未支持的多配置/生成器模式必须明确拒绝，不能误选 compile_commands 中的第一项。Debug/Release 切换仍须按共享状态失效，完整多配置产品支持不在当前已验收承诺中。
- 新/改 CMakeLists.txt 使用 `cmake_minimum_required(VERSION 4.0)`；C++ 与生成结果使用仓库 format/tidy 和实际编译数据库；提交/注释/文档中文，无工具署名。
- 定向联调可显式使用 `PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=ON` 并记录；正式最终验收固定已提交源码、override=OFF。测试源码进入 Tests；计划临时材料只放 `.superpowers/sdd/2026-11-09-pangumeta-tool/`，收尾归档后精确清理。
- 基线、被测 tree 和工作状态满足 AGENTS.md 的条件时复用全量结果；不因分支名或提交 SHA 变化重复全量。

## Review Focus

1. A→B→直接构建 A：共享对象文件比 A 的旧 Ninja 日志更新，仍须恢复 A 的实际内容；Task 1 `ProjectSwitchRestoresSamePathObjects` 验证 17→29→17 和末次 no-op。
2. 错误元数据 Owner、无 Service、关闭中借用及 moved-from 对象：不能错保其他 DLL 或泄漏 Pin；Task 3/12 `WrongOwnerRejectedBeforePublish`、`MetadataBorrowGatesAndPhysicalUnload` 固定。
3. AST 销毁后字符串 Key/别名/constexpr Options：IR 仍拥有正确语义，不依赖 AST 或宿主布局；Task 5/7 `ModelSurvivesAstContext`、`OptionsDefaultsAndAliases` 固定。
4. 已有输出后的部分发布失败、删除 BYPRODUCT、移除输入和外来文件：旧 stamp 不能掩盖失败，只清理本任务拥有的文件；Task 8/10 `PartialCommitCannotReuseOldStamp`、`MissingByproductRecovers`、`ForeignFilePreserved` 固定。
5. 同 basename、Unicode、已登记跨 Module 头及漏登记头、不同前端条件：不能覆盖或静默漏生成；Task 6/7/10 `OwnershipAndConditionGuards`、`OutputMappingIsInjective` 固定。

---

## 文件职责、构建入口与跨任务接口

本功能是一个有依赖的生成/消费闭环，不拆成相互独立的子系统计划。Task 1 的分根与切换是先导门；先证明原生接线，再接入 SDK 与生成任务。已有大文件只增加对应小接缝，不整体重写 Manager/ScopeRuntime。

| 位置 | 新增/修改职责 |
| --- | --- |
| `cmake/PanguEngine.cmake`、`cmake/PanguBuildLayout.cmake`、`cmake/PanguRuntimeDeployment.cmake`；根 `CMakeLists.txt`、`cmake/Platform/Common/Module.cmake`、`cmake/PanguPlugins.cmake` | 同图添加 Engine、所有权目录、共用对象依赖、当前 Project 状态与必要运行库部署 |
| `BuildTools/PanguBuildTool/Private/BuildLayout.{h,cpp}`、`Main.cpp`、工具 CMake | C++ 计算/校验规范路径及派生布局状态，保留 discover CLI |
| `Source/Runtime/Foundation/Public/Meta{Identity,Options,Info,Traits}.h` | TypeId、Options、只读描述、受约束字段访问和 Traits 声明，均不链接上层 |
| `Source/Runtime/Module/Public/ModuleMeta{Provider,Borrow}.h`；`Private/ModuleMetadataAccess.{h,cpp}` | 模块局部不可变提供者、仅移动 Pin 借用、受检访问桥 |
| `Source/Runtime/Module/Private/{ModuleRecord.h,ModuleCatalog.cpp,ModuleManager.cpp}`；Foundation 的 `ModuleDescriptor.h` | 接纳验证及可选提供者指针、业务 Gate、卸载前撤销引用 |
| `Source/Runtime/EngineHost/Public/HostMetaAccess.h`、`Private/HostMetaAccess.cpp`、`Public/EngineHost.h` | Ready/Busy 前置检查与已知类型获取接口 |
| `BuildTools/PanguMetaTool/{CMakeLists.txt,Private/Main.cpp}`、`Sdk/{Check.cmake,Prepare.cmake}` | 独立宿主工具、SDK 严格预检与显式准备 |
| 工具下 `Driver/`、`MetaIR/`、`Attr/`、`CodeGen/`、`Output/` | 请求/上下文、拥有数据的模型、Clang 适配、确定性 C++ 输出、文件批次发布；不是五个 Runtime Module |
| `cmake/{PanguMetaTool,PanguMeta}.cmake`、`cmake/BuildMetaTool.cmake` | 宿主工具原生依赖、头/身份/Host 登记、固定输出及 Depfile/stamp 接线 |
| `Tests/BuildTools/PanguMetaTool/`、`Tests/CMake/PanguMeta/`、`Tests/Meta/` | 工具、原生构建图、实际描述/对象/物理卸载正式测试 |

### A. 分根与切换（Task 1）

- 新 `pangu_add_engine(SOURCE_DIR <checkout> ENGINE_BUILD_ROOT <absolute-dir>)` 供 Project 在 `project()` 后调用；内部仍 `add_subdirectory()`，不用 imported Engine 或第二张引擎构建图。standalone 以当前正式 binary root 初始化同一布局。
- 缓存键 `PANGU_ENGINE_BUILD_ROOT`，Project 根取顶层 `CMAKE_SOURCE_DIR` / `CMAKE_BINARY_DIR`。测试只能覆写到当前宿主正式 BuildRoot 下的隔离测试子目录。
- `pangu_add_owned_subdirectory(SOURCE_DIR <dir> OWNER engine|project RELATIVE_BINARY_DIR <relative> [EXCLUDE_FROM_ALL])` 选择所属根；`pangu_configure_module` 将 `PANGU_SOURCE_OWNER` 固化到目标。实际 Engine 目标 binary 子树固定为 `<EngineBuildRoot>/Targets/<source-relative>`，两种顶层图都使用该路径；对象不放 ProjectBuild/Engine。
- PanguBuildTool 新 CLI：`layout --engine-source <dir> --engine-build <dir> --project-source <dir> --project-build <dir> --mode Monolithic|Modular --config <config>`，stdout 为 version=1 布局 JSON，原 discover 行为不变。`pg::build::layout::Resolve(const Request&) -> std::expected<Layout, std::string>` 与 `WriteState(const Layout&) -> std::expected<void, std::string>` 定义在 BuildLayout.h；Request 四个 path、Mode/Config 字符串，Layout 含规范根、当前 Project 状态路径及上下文头路径。
- 当前 Project 身份/模式/配置写到 Engine 根 `Generated/BuildContext/CurrentProject.json` 与同目录 `CurrentContext.h`，两者都是当前活动配置共用文件，内容相同不写。不能只写各 Config 自己的旧头，否则 A(Debug)→B(Release)→A(Debug) 可能错误复用 B 的同路径对象。状态是派生构建输入，不是 ABI 收据；各顶层图登记共享状态为 `CMAKE_CONFIGURE_DEPENDS`。
- Engine 自有编译目标通过原生 `-include <CurrentContext.h>` 将共享上下文头纳入编译依赖；头只含上下文注释/防重复包含，不引入产品 API。A→B→A 时即使命令在 A 的旧日志中没变，头更新也会使旧对象失效；同一 A 的 no-op 不改头。库/Export 输出使用所属根，原生链接依赖保持。
- `pangu_stage_host_runtime(TARGET <host>)` 将 Windows Host 所需的 Engine Runtime DLL 以原生文件依赖、copy_if_different 部署到 Host 运行目录；实际库产物仍归 Engine 根。按目标运行依赖取文件，不导入动态业务 Module；同目的地去重生产者，DLL 改变但 Host 无需重链接时部署也更新，不能只靠 Host 的 POST_BUILD。Unix 保持原生 build RPATH；测试启动环境与直接运行 Host 均验证。

### B. Foundation 公共语义（Task 2）

- `pg::meta::TypeId { std::array<std::byte,16> Bytes{}; constexpr bool IsValid() const noexcept; operator<=> }`，与 ModuleId 不隐式转换。
- `FieldOptions { const char* Key{nullptr}; bool ReadOnly{false}; }`；`enum class Access : std::uint8_t { kPublic, kProtected, kPrivate };`。
- `FieldInfo { string_view Name, Key, TypeName; Access Visibility; bool IsConst, IsReadOnly; }`。IsReadOnly 表示规范化的属性政策，IsConst 单独表示语言事实；实际写入能力还取决于合法赋值，不把二者混成宿主偏移量。
- `TypeInfo { string_view Name; optional<TypeId> Id; span<const FieldInfo> Fields; }`。这些视图引用目标产物存储；IR 使用自身拥有的字符串/容器。
- `template<class T> struct TypeTraits;`；生成特化包含 `Owner`（ModuleId）、`Name`、`Id`、`HasTypedAccess`、`HasDescription`、`Fields`（仅 public 访问描述 tuple）。
- `FieldAccess<auto Member, bool PolicyReadOnly=false>` 含 `FieldInfo Info`，通过成员指针推导 Owner/Member/Value 类型；`Read(const Owner&) const noexcept -> const MemberType&`，删除两个 Owner 右值重载；`Write(Owner&, U&&) const noexcept(is_nothrow_assignable_v<MemberType&,U&&>)` 仅在精确值类型、非 const、非只读、可赋值时成立。`ForEachField<T>(Visitor&&)` 只对 HasTypedAccess 为 true 的特化成立，按声明顺序访问。

### C. 运行期身份闭合（Task 3）

这是本计划的实现选择：给原 `ModuleDefinition` 末尾增加默认空的 `const ModuleMetaProvider* Metadata{}`，Foundation 只前置声明该类型，沿用现有 RegistrationContext 前置声明模式，不包含 Module 头。原 Descriptor/Register 由业务 C++ 保持；启用描述的 Module 手写一项 `.Metadata = &生成提供者`，不新增回调/注册阶段，也不重建整个 Definition。

- Module 的 `ModuleMetaProvider.h` 定义 `ModuleMetaEntry { const std::type_info* CppType; const meta::TypeInfo* Info; }`、`ModuleMetaProvider { ModuleId Owner; span<const ModuleMetaEntry> Entries; }`。这是单 Module 的不可变表，不是全局 Registry；不按字符串查询。
- 人工协议夹具固定 ModuleId=`e1000000000000000000000000000003`，错 Owner=`e1000000000000000000000000000004`，类型限定名 `pg::test::ManualType`；Task12 的生成夹具使用另两项身份，避免跨测试域碰撞。
- CopyDefinition 在已持有原生库的接纳窗口验证 Metadata Owner 等于 Descriptor.Id、非空项、重复 RTTI 值及显式 TypeId；不一致返回既有 `ModuleError::kInvalidDefinition`，不追加/重编号错误。未提供 Metadata 仍合法。
- ModuleRecord 保存受原生所有权保护的提供者借用；FinishRecord 在原生释放前清空元数据引用，不让 RTTI/表指针进入残留控制块。保留实际 Definition 传来的提供者，不接收应用提供的回调。
- `ModuleMetaBorrow`：默认空、仅移动，内部 `ModuleCodePin` + `const meta::TypeInfo*`；`explicit operator bool() const noexcept`、`Get() const & noexcept -> const meta::TypeInfo*`、`Release() noexcept`。移动后源对象为空，Release 同时清空视图与 Pin，不触发后台卸载。
- 私有 `ModuleManager::AcquireMetadata(ModuleId, const std::type_info&) const noexcept -> Result<ModuleMetaBorrow>`，通过 detail `ModuleMetadataAccess::Acquire(const ModuleManager&, ModuleId, const type_info&)` 暴露给宿主桥。复用现有 Pin 接纳，不 const_cast Manager，不返回 Manager 或裸 provider。
- 错误顺序：Manager Busy→kBusy；无状态/关闭/整体关闭→kInvalidState；Owner 不存在→kNotFound；记录未 Active/业务 Gate 关闭→kNotReady；无 Metadata/无匹配 RTTI→kNotFound。比较 RTTI 值，不比较地址或仅名称哈希。
- `pg::engine::detail::HostMetaAccess::Acquire(const EngineHost&, ModuleId, const type_info&) -> Result<ModuleMetaBorrow>`，Busy/非 Ready 使用现有 Host 错误域。高层头提供 `pg::meta::BorrowTypeInfo<T>(const pg::engine::EngineHost&) noexcept -> Result<module::ModuleMetaBorrow>`，仅 HasDescription 的生成特化可用；内部从 TypeTraits<T>::Owner 绑定身份。Foundation 与生成的纯访问头不包含 EngineHost。
- 当前同树 Public ABI 未冻结，所有受影响 Module/夹具必须重编；不承诺旧二进制 Definition 布局兼容，保持既定 `PanguModuleQueryV1` / ProtocolVersion=1 的受控源码消费边界。

### D. 工具数据和文件协议（Task 4–9）

- 工具私有 `pg::meta::tool::Diagnostic { std::string Code, Message; ir::SourceLocation Source; }`，`Diagnostics=vector<Diagnostic>`，`template<class T> using ToolResult=expected<T,Diagnostics>`；失败不得靠 C++ Exception。
- 独立宿主图分为普通静态实现目标 `PanguMetaModel`（请求/IR，无 LLVM）、`PanguMetaFrontend`（SDK/上下文/Attr）、`PanguMetaOutput`（发布）、`PanguMetaCodeGen`（发射），再链接可执行 PanguMetaTool。它们不是 Runtime Module，不提供 Pangu:: Alias；纯 Model/Output/CodeGen 单测不通过链接便利性反向依赖 Frontend。
- `pg::meta::ir::SourceLocation { std::string File; uint32_t Line,Column; }`；FieldDefinition 拥有 Name/Key/CppType/Source/Visibility/IsConst/ReadOnly；TypeDefinition 拥有 QualifiedName、optional<TypeId>、Source、Fields；Model 拥有 ContextId 与 Types。无 AST 借用。同命名空间 `Normalize(Model) -> tool::ToolResult<Model>` 及 `ParseTypeId(string_view) -> tool::ToolResult<meta::TypeId>`；Driver/Scan/Generate/Publish 接口属于 `pg::meta::tool`。
- `GenerationRequest` 与 IR 分离，位于 Driver/GenerationRequest.h。v1 JSON 必需公共项：`version, kind, target, owner, config, engine_source, engine_build, project_source, project_build, output_root, module, headers, ownership, context, catalog, outputs`；kind 为 scan/query/catalog。不适用项用空数组/对象，未知字段/类型/版本拒绝。
- 原生请求类型同时定义：`TaskKind{kScan,kQuery,kCatalog}`、`SourceOwner{kEngine,kProject}`、`HeaderVisibility{kPublic,kPrivate}`；`HeaderInput{path Path,Root; string Prefix; HeaderVisibility Visibility; bool TypedAccess,Description;}`、`OwnershipEntry{path Path; string Target;}`、`ContextInput{path Database,Source,Object,ResourceDir;}`、`ModuleBinding{string IdSymbol,IdentityHeader,QuerySymbol,QueryHeader,ExportMacro;}`、`CatalogEntry{string Target,QuerySymbol,QueryHeader;}`。GenerationRequest 逐项对应上述 JSON：Version 为 uint32_t，Kind/Owner 为对应 enum，Target/Config 为 string，EngineSource/EngineBuild/ProjectSource/ProjectBuild/OutputRoot 为 path，其余为对应结构/vector。
- module 为 `id_symbol, identity_header, query_symbol, query_header, export_macro`；不含 ID 值。headers 每项为 `path, root, prefix, visibility, typed_access, description`；ownership 每项为 `path,target`，来自当前图全部 Meta 登记。context 为 `database, source, object, resource_dir`，准确定位所属目标的实际命令。catalog 每项为 `target,query_symbol,query_header`，保留显式顺序，不推断运行期依赖。
- context.object 在 source 唯一匹配当前目标的专属 MetaContext.cpp 时可为空；否则须给出精确对象路径，缺失或多匹配拒绝。CMake 默认生成每 Target/Config 专属的空 MetaContext.cpp，因此不猜 CMake 的对象文件哈希/缩短规则。ResourceDir 指 SDK 资源根（其下有 include/stddef.h），不是直接传 include 子目录。
- output_root 必须等于所属根的 Generated/PanguMeta/Config/Target；outputs 为已知相对路径。默认 prefix 为空，同 visibility 的额外输入根显式给不同 prefix。映射保留源根内相对目录；同 basename、保留汇总文件名冲突、大小写/路径别名冲突提前拒绝。C++ 标识符转义逐 UTF-8 字节保留 ASCII 字母数字，其余（含下划线）写 `_XX`，前缀 `T_`，避免歧义碰撞。
- `InputOwnership::Create(span<const OwnershipEntry>) -> ToolResult<InputOwnership>` 深复制归属表；`Find(const filesystem::path&) const -> ToolResult<optional<string_view>>` 的视图仅借用该表，Scan 期间保持表存活，IR 复制所需字符串。
- `ReadRequest(const filesystem::path&) -> ToolResult<GenerationRequest>`；`BuildInvocation(const GenerationRequest&, const HeaderInput&) -> ToolResult<vector<string>>`；`Scan(const GenerationRequest&) -> ToolResult<ScanResult>`；`Generate(const GenerationRequest&, const ir::Model&) -> ToolResult<vector<GeneratedFile>>`。`ScanResult { ir::Model Model; vector<filesystem::path> Dependencies; }` 在 Attr/Scan.h 定义，保存所有实际读取文件的规范路径，构建依赖不混进 Type/Field IR。query/catalog 模式传空 Model，直接消费装配请求，不塞入 Type IR。
- `GeneratedFile { filesystem::path RelativePath; std::string Content; }`；`Publish(const GenerationRequest&, span<const GeneratedFile>, span<const filesystem::path> dependencies) -> ToolResult<void>`。scan 传 ScanResult.Dependencies，query/catalog 传实际读取的派生请求/输入；Driver 另纳入 request、工具和 context 数据库的原生依赖。批次私有状态文件为 Meta.outputs.json、Meta.d、Meta.stamp；不是 IR 导出。v1 outputs index 只存该任务拥有的相对路径，Depfile 正确转义空格、#、$、反斜线和 Windows 盘符。
- CLI：`PanguMetaTool --check-sdk`、`PanguMetaTool generate --request <absolute-json>`；成功 0，解析/语义/生成/发布失败 1，CLI 用法错误 2；诊断 stderr，成功不打印 IR。Windows 使用 wmain 保持路径原生表示。
- 诊断码固定为小写 snake_case：`invalid_request`、`invalid_context`、`invalid_type_id`、`duplicate_type_id`、`duplicate_field_key`、`duplicate_owner`、`unregistered_input`、`unsupported_declaration`、`unsupported_condition`、`orphan_field`、`invalid_options`、`invalid_constant`、`output_collision`、`publish_failed`、`sdk_rtti_required`、`sdk_stdlib_mismatch`、`incompatible_sdk`；Clang 原生错误保留源位置与原文，不假装都能映射成一种属性错误。

### E. 原生登记和产物（Task 10–11）

- `pangu_meta_headers(TARGET <t> ROOT <dir> [PREFIX <rel>] VISIBILITY public|private EMIT typed_access|description|both|check HEADERS <relative...>)`：同一入口完成 target_sources 与 Meta 登记，处理重复/归属/消费并集，不另写类型清单。
- `pangu_meta_consume(TARGET <consumer> MODULE <owner> HEADERS <already-registered-relative...> EMIT typed_access|description|both [SCOPE PRIVATE|PUBLIC|INTERFACE])`：头引用相对 owner 的 SOURCE_DIR（如 Public/Actor.h），也可使用对应已登记的绝对路径；只增加公共头消费需求，不重复取得所有权，也不能消费 Private 生成头。默认 SCOPE=PRIVATE。按实际需求向消费者接线原始公开头、Export/生成包含路径、生成顺序和属性诊断豁免，不隐式链接业务 DLL。
- 纯构建用 `PanguMetaUsage_<owner>` INTERFACE 目标承载头/选项/生成依赖，不产库、不链接所属业务 Module；PUBLIC 消费依赖沿此接口传递。消费者调用上述函数后无需复制 include 路径/生成依赖。每目标的私有 `PG_META_HAS_DESCRIPTION=0|1` 由需求并集派生，业务 Definition 可据此有条件包含 ModuleMeta.gen.h 并设置 Metadata；关闭描述时不留下必需生成头或数据符号。
- `pangu_meta_module(TARGET <t> ID <qualified-constant> ID_HEADER <header> QUERY <qualified-query> HEADER <header>)`：记录实际目标与公共 C++ 身份符号；`pangu_meta_host_catalog(TARGET <host> MODULES <actual-targets...>)`：记录该 Host 显式集合。目标图登记完成后延迟汇总，生成每目标一份派生请求。
- pangu_export_module_query 在迁移后转调 pangu_meta_module；新增 ID/ID_HEADER 参数，Monolithic 也保存 Query 登记。每个迁移调用点填写已有常量符号，值不重复；原 CMake 包装生产者同时撤销。
- 任何 scan 输出模式都按需生成伴随 `.gen.h`，声明 Traits 及其 Has* 能力；typed-only 不产生 Runtime 全字段表。description 另生成每头 `.gen.cpp` 和 Module 私有 `ModuleMeta.gen.h/.gen.cpp`；输入增删通过请求更新槽位。
- 每头实现定义其 TypeInfo 常量；Module 汇总包含原始类型头及对应常量声明，建立 constexpr RTTI/Info 地址表，导出 `pg::meta::generated::<转义Target>::kModuleMetadata`。通过静态初始化表达式实现，不依赖全局构造注册；业务 ModuleDefinition 引用该变量。
- 原生包装 `Private/ModuleQuery.gen.cpp` 仅 Modular；Host 的 `Private/StaticModuleCatalog.gen.cpp` 仅 Monolithic。生成头和实现加入所属唯一目标，公开生成依赖传播给消费者，不要求手写 include .gen.cpp。
- 宿主工具根分别取 Windows/Linux/macOS 的正式宿主 BuildRoot 下 `BuildTools/PanguMetaTool`；测试工具根为 `Tests/BuildTools/PanguMetaTool`，与生产缓存分离。交叉目标复用该宿主程序，不导入 LLVM targets 到移动图。
- 宿主子工程配置导出 PanguMetaToolBuildInputs.cmake（源码/头/SDK 链接输入列表）；目标图给 exe 建 OUTPUT/DEPENDS，只有失效时运行 `cmake/BuildMetaTool.cmake` 的原生配置/构建。共享宿主缓存加短期 file(LOCK)，无变化不运行嵌套检查。

## 实施任务

### Task 1: 实际产物分根与 A→B→A 先导门

**Files:** Create `cmake/{PanguEngine.cmake,PanguBuildLayout.cmake,PanguRuntimeDeployment.cmake}`、`BuildTools/PanguBuildTool/Private/{BuildLayout.h,BuildLayout.cpp}`；Modify 根及 BuildTools/PanguBuildTool/CMakeLists.txt、该工具 Private/Main.cpp、cmake/PanguPlugins.cmake、cmake/Platform/Common/Module.cmake、Tests/CMake/SourcePluginProject/MyGame/CMakeLists.txt、ProjectTestHelpers.cmake 及测试启动接线；Create `Tests/CMake/PanguMeta/{CMakeLists.txt,BuildLayoutTests.cmake,ProjectSwitchTests.cmake,Fixtures/Project/CMakeLists.txt,Fixtures/Project/Main.cpp,Fixtures/EnginePlugin/CMakeLists.txt,Fixtures/EnginePlugin/Value.cpp}`，接入 Tests/CMakeLists.txt。

**Interfaces:** Produces A；既有 discover/Module 目标名保持。测试入口 PanguMetaBuildLayout、PanguMetaProjectSwitch，boundary=system、area=build、tier=full，隔离根在当前 BuildRoot/Tests/Work 下。

- [x] **Step 1: 写失败验收。** BuildLayoutTests 核对 Engine binary/object/Lib/Bin/Export 真落 Engine 根、Project 目标真落 Project 根、Engine/Project Plugin 归属及非法根拒绝；ProjectSwitchRestoresSamePathObjects 编译同一共享对象路径，A 定义值 17、B 为 29，直接构建旧 A 必须输出 17。另覆盖 A(Debug)→B(Release)→A(Debug)，比较对象路径相同、直接运行 Host 能找到正确 Runtime DLL、DLL 部署不依赖 Host 强制重链接，末次普通构建无编译/链接动作。

  ```cmake
  RequireText(resultA1 "value=17")
  RequireText(resultB "value=29")
  RequireText(resultA2 "value=17")
  ```
- [x] **Step 2: RED。** `cmake --preset windows`；`ctest --preset windows -R '^PanguMeta(BuildLayout|ProjectSwitch)$' --no-tests=error --output-on-failure`，预期根归属或共享切换断言失败；空匹配/缺环境不算 RED。
- [x] **Step 3: 实现 A。** 当前 Project 状态与强制包含头按内容写入，用原生 CMAKE_CONFIGURE_DEPENDS 触发旧图重配；当前头是共享对象失效依据，不能只依赖旧图 command hash。所有者由目录接线/Plugin origin 显式传递；调整全部引擎目标 binary 子目录，保留同图链接与平台根策略。
- [x] **Step 4: GREEN。** 重跑上述两例，加 `ctest --preset windows -R '^SourcePlugin(Project|StaticCatalog|SharedDeploy|Incremental)$' --no-tests=error --output-on-failure`；首次迁移旧缓存须明确诊断并定点迁移，不删其他任务树。核对 Windows 结果后以同样正式夹具证明 WSL/macOS 外部 binary 子树可用。
- [x] **Step 5: 提交。** 仅暂存本任务文件及必要规范状态更新，`git commit -m "feat: 按源码归属隔离 Engine 与 Project 构建产物"`。

### Task 2: Foundation 统一语义与强类型字段基础

**Files:** Create `Source/Runtime/Foundation/Public/MetaIdentity.h`、`MetaOptions.h`、`MetaInfo.h`、`MetaTraits.h`；Create `Tests/Foundation/MetaTraitsTests.cpp`、`MetaIdentityTests.cpp`；Modify Tests/Foundation/CMakeLists.txt。

**Interfaces:** Consumes ModuleIdentity.h 的 ModuleId；Produces B，纯头文件，不链接 Module 或工具。

- [x] **Step 1: 写失败测试。** `MetaIdentity.OptionalAndDistinct` 核对零 TypeId 无效、optional 空有效表示无稳定身份、不可隐式转 ModuleId。`MetaTraits.ExactWriteAndConstRead` 使用 `{ int Health{7}; const int Version{1}; float Speed{1.0F}; }`，声明断言：`static_assert(!CanWriteSpeed<int>); static_assert(CanWriteSpeed<float>);`，其中测试内 concept 只检测 B 的 Write 表达式；实际写 Speed=2.0F 并 EXPECT_FLOAT_EQ，const/ReadOnly/临时对象/数组赋值均编译拒绝，数组读取保留数组类型。

  ```cpp
  static_assert(!CanWriteSpeed<int>);
  static_assert(CanWriteSpeed<float>);
  EXPECT_FLOAT_EQ(actor.Speed, 2.0F);
  ```
- [x] **Step 2: RED。** `cmake --build --preset windows --target PanguFoundationUnitTests`，预期新 Meta 接口缺失的定向编译错误。
- [x] **Step 3: 实现 B。** 保留 cv、数组及对象类型，不做类型擦除；ForEachField 以 tuple/apply 实现声明顺序，不暴露可绕过政策的可写引用。
- [x] **Step 4: GREEN。** 构建同目标，`ctest --preset windows-fast -R 'Meta(Identity|Traits)[.]' --no-tests=error --output-on-failure`；新增头逐个独立包含验证；按实际数据库 format/tidy。
- [x] **Step 5: 提交。** `git commit -m "feat: 建立统一 Meta 描述与强类型字段基础"`，只暂存本任务文件。

### Task 3: Module 接纳绑定与 EngineHost 描述借用

**Files:** Create `Source/Runtime/Module/Public/ModuleMetaProvider.h`、`ModuleMetaBorrow.h`、`Private/ModuleMetaBorrow.cpp`、`ModuleMetadataAccess.h`、`ModuleMetadataAccess.cpp`；Create EngineHost 的 Public/HostMetaAccess.h、Private/HostMetaAccess.cpp；Modify Foundation/Public/ModuleDescriptor.h、Module/Public/ModuleManager.h、Private/ModuleRecord.h、ModuleCatalog.cpp、ModuleManager.cpp、EngineHost/Public/EngineHost.h 及两个模块 CMakeLists.txt；Create `Tests/Module/ModuleMetadataTests.cpp`、`Tests/EngineHost/EngineHostMetadataTests.cpp`、`Tests/Meta/Fixtures/ManualModule.{h,cpp}`、`Tests/Meta/Fixtures/CMakeLists.txt`；修改对应测试 CMake。

**Interfaces:** Consumes B、既有 Find/TryPinForBusiness/Gate；Produces C。人工提供者夹具只用来独立固定接纳契约，Task 12 另测真正生成产物。

- [x] **Step 1: 写失败测试。** `ModuleMetadata.WrongOwnerRejectedBeforePublish` 对 Definition.Id=A、Metadata.Owner=B 返回 kInvalidDefinition；Entry 的空 CppType/Info、重复 RTTI/重复显式 ID 同样拒绝，Definition.Metadata=nullptr 仍合法。`EngineHostMetadata.NoServiceAndClosing` 无服务但 Active 时成功：`auto info = pg::meta::BorrowTypeInfo<ManualType>(host); ASSERT_TRUE(info); EXPECT_EQ(info->Get()->Name, "pg::test::ManualType");`，随后 BeginShutdown、再次获取失败，已有借用读表仍有效，移动源为空，Release 后 Finish 成功。夹具中手写 TypeTraits 只绑定 A，无任意 provider 回调参数。

  ```cpp
  auto info = pg::meta::BorrowTypeInfo<ManualType>(host);
  ASSERT_TRUE(info);
  EXPECT_EQ(info->Get()->Name, "pg::test::ManualType");
  ```
- [x] **Step 2: RED。** 构建 PanguModuleIntegrationTests 与 PanguEngineHostTests，预期新借用/桥接口缺失；再运行定向两组确认失败分类。
- [x] **Step 3: 实现 C。** 新 Metadata 指针默认空，不修改业务 Register；验证与保存发生于真实接纳窗口，按 RTTI 值比较。Manager 私有入口复用 Pin Gate，Host 桥按 Operating→Ready 检查；移动/Release 后不残留视图。
- [x] **Step 4: GREEN。** `ctest --preset windows-fast -R '(ModuleMetadata|EngineHostMetadata)[.]' --no-tests=error --output-on-failure`；加 Module 生命周期/EngineHost 关闭定向回归，检查 invalid state、wrong Owner、无 Metadata、未 Active、Busy 重入错误顺序；所有受影响模块重编。
- [x] **Step 5: 提交。** `git commit -m "feat: 接纳模块元数据并提供受保护描述借用"`。

### Task 4: 显式 SDK 准备与独立宿主工具预检

**Files:** Create `BuildTools/PanguMetaTool/CMakeLists.txt`、`BuildTools/PanguMetaTool/Private/Main.cpp`、工具下 `Driver/{Diagnostic.h,SdkInfo.h,SdkInfo.cpp}`、`MetaIR/SourceLocation.h`、`Sdk/{Check.cmake,Prepare.cmake}`、`README.md`；Create `Tests/BuildTools/PanguMetaTool/{CMakeLists.txt,SdkTests.cmake,SdkLinkTests.cpp}`，接入 Tests/CMakeLists.txt 的现有 BuildTools 调度。后续任务“工具下”均指 BuildTools/PanguMetaTool，“正式工具测试下”均指 Tests/BuildTools/PanguMetaTool。

**Interfaces:** Produces `PanguMetaTool --check-sdk`；`pg::meta::tool::SdkInfo { std::string FrontendVersion; filesystem::path ResourceDir; bool HasRtti; std::string HostStdlib; }`；同命名空间 `CheckSdk() -> ToolResult<SdkInfo>`，Diagnostic/ToolResult 在 Driver/Diagnostic.h 定义，SourceLocation 定义于 MetaIR/SourceLocation.h（下一任务复用）。SDK 配置缓存 PANGU_META_LLVM_DIR、PANGU_META_CLANG_DIR、PANGU_META_RESOURCE_DIR、PANGU_META_HOST_COMPILER；Check/Prepare 均接受 PG_SDK_CONFIG_FILE，预检成功后生成只设置这些路径及来源版本的 PanguMetaSdk.cmake。根参数 PANGU_META_SDK_CONFIG 引用该本机构建状态文件，不将 LLVM target 导入父图，不提交机器路径。

- [x] **Step 1: 写失败验收。** `MetaSdk.RejectsNoRtti`、`RejectsWrongStdlib`、`AcceptsCompatibleSdk` 分别检查明确配置诊断、真正跨 SDK 基类的 typeid/派生链接及一个原始 C++ TU 解析；不能只相信配置字符串。缺 SDK 是前置条件失败，不能冒充 RED。

  ```cpp
  const auto info = CheckSdk();
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->HasRtti);
  ```
- [x] **Step 2: 准备 SDK 并记录输入。** 已兼容 SDK可复用；Windows 无 RTTI SDK 须整改。Prepare.cmake 显式接收 PG_LLVM_SOURCE_DIR、PG_LLVM_SOURCE_COMMIT、PG_SDK_BUILD_ROOT、PG_HOST_COMPILER，核对 git HEAD，不联网/checkout 或覆盖其他安装；已验证参考提交为 `ea7d852a70e8bdfaf601d6626a760f9771b2c4b4`，复验/升级记录实际来源。原生 LLVM CMake 使用 RTTI=ON、EH=OFF、Linux/macOS LIBCXX=ON、TARGETS_TO_BUILD=X86;AArch64，构建 `llvm-libraries`、`clang-libraries`、`clang-resource-headers` 形成可用开发包/构建树导出，覆盖 clangTooling/Frontend/Sema/AST/Basic 依赖；不构建替代目标 clang。聚合目标已核对参考提交的上游 CMake 源码，不能直接复用忽略目录中的旧 tar 作为正式来源。
- [x] **Step 3: RED→实现预检。** 先用不兼容包触发测试所需诊断；再独立宿主工程 find_package LLVM/Clang 23.x，并通过真实编译/链接检查，SDK 头 SYSTEM、自有代码严格警告/无异常/RTTI。禁止以 -fno-rtti 修复 Attr。仅 --check-sdk 的 Driver 在本任务可用，其余 CLI 用法返回2。
- [x] **Step 4: GREEN。** `cmake -S BuildTools/PanguMetaTool -B Build/Windows/Tests/BuildTools/PanguMetaTool -G Ninja -DCMAKE_CXX_COMPILER:FILEPATH=$env:LLVM_ROOT/bin/clang++.exe -DBUILD_TESTING=ON -DPANGU_META_LLVM_DIR:PATH=<已准备SDK的LLVM配置目录> -DPANGU_META_CLANG_DIR:PATH=<同一SDK的Clang配置目录> -DPANGU_META_RESOURCE_DIR:PATH=<对应资源目录>`；构建 PanguMetaTool/PanguMetaSdkTests，再 `ctest --test-dir Build/Windows/Tests/BuildTools/PanguMetaTool -R '^MetaSdk' --no-tests=error --output-on-failure`。尖括号为本任务产出的显式机器配置值，保存到验收记录，不提交绝对路径。
- [x] **Step 5: 提交。** `git commit -m "feat: 建立 PanguMetaTool 宿主 SDK 准备与严格预检"`。

### Task 5: 派生请求、拥有数据的 MetaIR 与规范化

**Files:** Create 工具 `Driver/{GenerationRequest.h,GenerationRequest.cpp}`、`MetaIR/{Model.h,Normalize.h,Normalize.cpp}`；Modify 工具 CMake、Private/Main.cpp 及正式工具测试 CMakeLists.txt；Modify Source/ThirdParty/NlohmannJson/CMakeLists.txt 允许经验证的独立宿主消费（不伪造 Runtime PanguBuildOptions）；Create 正式工具测试下 `RequestTests.cpp`、`ModelTests.cpp`。

**Interfaces:** Consumes B、Task 4 的 Diagnostic/SourceLocation；Produces D 的请求/IR/Normalize/ParseTypeId，独立工具只包含 Foundation 轻量头，不链接 Foundation 库以避免 bootstrap 环。

- [x] **Step 1: 写失败测试。** `MetaRequest.SchemaAndPaths` 拒绝 version=2、未知项、错类型、重复键、绝对/逃逸输出、重复输出及保留名碰撞；`MetaIR.OptionalIdentityAndMerge` 断言无 ID 合法、全零显式 ID 拒绝、同一 canonical 声明重复合并、同名不同定义与重复显式 ID 报错。`MetaIR.OrderIsDeterministic` 打乱 TU 输入顺序后输出类型按规范限定名排序，字段保持声明顺序，结果一致。

  ```cpp
  const auto id = ParseTypeId("00000000000000000000000000000000");
  ASSERT_FALSE(id);
  EXPECT_EQ(id.error().front().Code, "invalid_type_id");
  ```
- [x] **Step 2: RED。** 构建 PanguMetaRequestTests/PanguMetaModelTests，运行 `ctest --test-dir Build/Windows/Tests/BuildTools/PanguMetaTool -R '^Meta(Request|IR)[.]' --no-tests=error --output-on-failure`，预期接口/语义缺失。
- [x] **Step 3: 实现 D 数据模型。** JSON_NOEXCEPTION，parse 禁异常且所有 get 前校验类型；拒绝重复键而非采用最后值。Key/文本拥有数据，读写路径保持原生表示，诊断UTF-8；真实文件别名用 filesystem 等价关系校验，不靠字符串小写模拟所有平台。
- [x] **Step 4: GREEN。** 重跑同组；复制/移动 Model 并销毁输入数据后仍得到原 Key/Source/Id；检查库依赖没有 Module/EngineHost/Clang 进入纯 Model 单测。
- [x] **Step 5: 提交。** `git commit -m "feat: 定义 MetaIR 与受检生成请求"`。

### Task 6: 精确编译上下文与输入所有权

**Files:** Create 工具 `Driver/{CompilationContext.h,CompilationContext.cpp,InputOwnership.h,InputOwnership.cpp}`；Create `Tests/BuildTools/PanguMetaTool/ContextTests.cpp`、`OwnershipTests.cpp` 与 Fixtures/Contexts 下的请求/响应文件样本；Modify 工具/测试 CMake。

**Interfaces:** Produces D 的 BuildInvocation；InputOwnership::Find(path) 返回已登记的唯一 Owner，普通传递头无 pga 时不要求成为输入；上下文选择同时匹配 context.source/object 与目标/配置，默认无歧义猜测。

- [x] **Step 1: 写失败测试。** `MetaContext.SelectsExactObject` 同源两目标宏为17/29，分别选对命令；缺失或多匹配失败。`ResponseFilesPreserveArguments` 覆盖空格/中文、嵌套响应文件、循环、缺文件及 commands/arguments 两种数据库形式。`WarningSuppressionCannotHidePgaTypo` 使用 SDK SyntaxOnlyAction 解析未知 `pga::Tyoe`，请求含 -w/-Wno-everything/-Wno-error 与未知属性豁免时仍须失败；此例无需尚未实现的 Type/Field 注册器。`MetaOwnership.OutputMappingIsInjective` 覆盖两个根同 basename、大小写别名、prefix 冲突、ModuleMeta 保留名、越界路径。

  ```cpp
  const auto invocation = BuildInvocation(requestForB, header);
  ASSERT_TRUE(invocation);
  EXPECT_NE(std::ranges::find(*invocation, "-DPG_TEST_VALUE=29"), invocation->end());
  ```
- [x] **Step 2: RED。** 构建 PanguMetaContextTests；`ctest --test-dir Build/Windows/Tests/BuildTools/PanguMetaTool -R '^Meta(Context|Ownership)[.]' --no-tests=error --output-on-failure`。
- [x] **Step 3: 实现上下文。** 展开响应文件后剔除编译器程序、原TU、-c、-o/dep 输出；保留目标/sysroot/标准库/宏/include/语言设置，改用 SDK 匹配资源头与指定原始头。去除 -w/-Wno-everything 等会屏蔽校验的整组开关及未知属性豁免，最后显式启用 unknown-attributes 并升级为错误；不传 PCH/Modules 二进制参数，只有可还原为原始头的形式可继续；未知/不适用 driver 模式报错，不静默回退宿主。
- [x] **Step 4: GREEN。** 重跑同组，用 Task 4 SDK 实际解析生成 invocation 的最小 TU，对照目标平台/架构宏；别名与所有权检查只输出派生请求诊断，不提前生成 C++。
- [x] **Step 5: 提交。** `git commit -m "feat: 精确选择 Meta 编译上下文并校验输入归属"`。

### Task 7: Attr、Options 常量语义与 AST 无关模型

**Files:** Create 工具 `Attr/{AttributeSchema.h,TypeAttribute.h,TypeAttribute.cpp,FieldAttribute.h,FieldAttribute.cpp,ClangAttributeAdapter.h,OptionsReader.h,OptionsReader.cpp,ConditionGuard.h,ConditionGuard.cpp,Scan.h,Scan.cpp}`；Create 正式工具测试下 `AttrTests.cpp`、`Fixtures/Attr/{TypeCases.h,OptionsCases.h,UnsupportedCases.h,Dependency.h,Unregistered.h,ConditionCases.h}`；Modify 工具与测试 CMake。

**Interfaces:** Consumes B、D、Task 6 的 invocation/ownership；Produces `Scan(const GenerationRequest&) -> ToolResult<ScanResult>`，成功返回前已销毁本批 ASTContext 并完成 Normalize；结果包含拥有数据的 Model 和实际读取依赖。`RegisterBuiltinAttributes() -> void` 在首次解析前执行；具体 Type/Field Schema 派生 Clang Spellings/参数规则，单次值不存入共享注册器。匿名成员值类型的显示名用稳定的 `decltype(限定Owner::字段名)`，不把 Clang 打印的机器绝对路径发射到目标字符串。

- [x] **Step 1: 写失败测试。** `MetaAttr.TypeWithoutId` 解析 `struct [[pga::Type]] Actor`，断言 Id 为空；`OptionsDefaultsAndAliases` 覆盖 Key 默认字段名、显式 UTF-8/引号/换行键、constexpr 别名/聚合/工厂返回、ReadOnly 默认 false 与显式 true。`OwnershipAndConditionGuards` 断言孤立 Field、漏登记 pga 头、错误 namespace/case、重复属性/Key、非法 ID、错误 Options 类型、内嵌零/运行期值和不支持条件分别失败，已登记其他 Module 只检查不发射。`ModelSurvivesAstContext` 在 Scan 返回及临时输入销毁后逐字段检查拥有数据，再交给后续 CodeGen。

  ```cpp
  const auto scan = Scan(request);
  ASSERT_TRUE(scan);
  EXPECT_FALSE(scan->Model.Types.front().Id.has_value());
  EXPECT_EQ(scan->Model.Types.front().Fields.front().Key, "Health");
  ```
- [x] **Step 2: RED。** 构建 PanguMetaAttrTests，`ctest --test-dir Build/Windows/Tests/BuildTools/PanguMetaTool -R '^MetaAttr[.]' --no-tests=error --output-on-failure`；预期 Scan/Attr 行为缺失，不能拿 crash 或 SDK 缺失当作正确负例。
- [x] **Step 3: 实现 Attr。** 用 Clang Sema 求默认值/常量，核对实际 MetaOptions.h 的 canonical 声明而非同名假类型；别名合法。可用临时 AnnotateAttr 保存规范化值，Collector 再深复制到 IR，禁止进程全局“当前扫描”指针。孤立 Field 在完整声明形成后检查，避免 Type 标记处理顺序误报。私有字段只取描述，不发访问表达式；生成 TU 无法合法命名的标注类型明确诊断，不注入 friend。
- [x] **Step 4: GREEN。** 重跑 Attr 组；补齐位域/reference/static/union/template/仅业务 cpp、Private 字段与可见性负例；Driver/Sema 任一 Error/Fatal 都使 Scan 失败，包括 ClangTool 返回0的 Driver 错误。项目自有反射声明区间中识别两端不一致的前端条件并拒绝，普通平台宏通过；若项目输入用 diagnostic pragma 实际屏蔽属性检查，应报明确自有错误而不是漏生成，系统头合法抑制不一刀切拒绝。将 Task6 拼写负例复用于完整 Scan，确认正确属性注册不会使未知拼写逃过检查。
- [x] **Step 5: 提交。** `git commit -m "feat: 解析并规范化 Type 与 Field 属性语义"`。

### Task 8: 按内容差异发布、失败恢复与拥有文件清理

**Files:** Create 工具 `Output/{GeneratedFile.h,Publish.h,Publish.cpp}`、`Output/Platform/Windows/FileReplace.cpp`、`Output/Platform/Posix/FileReplace.cpp`；Create 正式工具测试下 `PublishTests.cpp`；Modify 工具/测试 CMake。

**Interfaces:** Produces D 的 GeneratedFile/Publish；平台私有 `ReplaceFile(const path& staged, const path& destination) -> ToolResult<void>`。Publish 只处理请求 outputs 中的相对文件；另有私有重载 `Publish(req, files, dependencies, const PublishOptions&)`，其中 BeforeReplace 为 `std::function<ToolResult<void>(const path&, std::size_t)>`（生产默认空），不进入用户 CLI。

- [x] **Step 1: 写失败测试。** `MetaPublish.SameContentKeepsTimestamp` 两次发布内容一致且 mtime 不变；`PartialCommitCannotReuseOldStamp` 初次成功后在第二文件替换前注入失败，检查返回失败、旧成功标记不被当成本轮成功、下轮修复全部结果。`ForeignFilePreserved` 删除输入后只移除旧 outputs index 中废弃文件；外来文件保留。损坏 index、输出链接逃逸及不属于 output_root 的替换/删除在写入前拒绝。

  ```cpp
  EXPECT_FALSE(failedPublish);
  EXPECT_FALSE(std::filesystem::exists(request.OutputRoot / "Meta.stamp"));
  EXPECT_TRUE(std::filesystem::exists(request.OutputRoot / "foreign.txt"));
  ```
- [x] **Step 2: RED。** 构建 PanguMetaPublishTests；`ctest --test-dir Build/Windows/Tests/BuildTools/PanguMetaTool -R '^MetaPublish[.]' --no-tests=error --output-on-failure`。
- [x] **Step 3: 实现发布。** 先验证所有路径/输出集合和准备同文件系统 staging，进入替换阶段前移除旧 stamp，按内容比较、逐文件原子替换；stamp 最后提交，不伪装多文件OS事务。Meta.outputs.json 只列拥有的相对路径，清理逐文件执行，删除前核对 resolved 根；不递归删除目标目录或未登记文件。
- [x] **Step 4: GREEN。** 重跑发布组及真实 Windows/Posix ReplaceFile 平台用例；验证候选写入失败、替换失败、受控中断、遗留 staging、丢失产物均可重试。无新输出的语义失败不得进入 Publish。
- [x] **Step 5: 提交。** `git commit -m "feat: 可靠发布 Meta 产物并恢复失败批次"`。

### Task 9: 强类型 C++ 与描述提供者生成

**Files:** Create 工具 `CodeGen/{Generate.h,Generate.cpp,CppNames.h,CppNames.cpp,TypeEmitter.h,TypeEmitter.cpp,ModuleEmitter.h,ModuleEmitter.cpp}`、`Driver/{RunGeneration.h,RunGeneration.cpp}`；Modify Private/Main.cpp 及工具/测试 CMake；Create 正式工具测试下 `CodeGenTests.cpp`、`CliTests.cmake`、`Fixtures/Consumer/{CMakeLists.txt,Actor.h,Main.cpp,Module.cpp}`。Query/Catalog 发射器由 Task11 实现，避免在此任务重复承担装配迁移。

**Interfaces:** Produces D 的 Generate 及 `RunGeneration(const GenerationRequest&) -> ToolResult<void>`，scan 依次取得 ScanResult，将 Model 传 Generate、Dependencies 传 Publish；query/catalog 分派点保留，Task11 接入前明确报告暂未实现，不生成假产物。`QuoteCppString(string_view) -> string` 和 `EscapeIdentifier(string_view) -> string` 分别处理源代码字面量与符号。

- [x] **Step 1: 写失败消费。** `MetaCodeGen.TypedAndDescriptionModes` 对同一 Actor 生成 typed-only 与 both：前者无 ModuleMeta 表/描述 cpp，后者可通过 Task 3 借用枚举完整字段。消费者执行 `ForEachField<Actor>`，对 Speed 精确写入 `2.0F` 并用普通 C++ 检查；const、ReadOnly、Private 的拒绝用独立 compile-fail 夹具核对准确诊断，不把缺头当负例。`StringEscapesCompile` 的 Key 含引号、反斜线、换行及控制字节后跟十六进制字符，生成后严格编译并比较字节完全一致。

  ```cpp
  EXPECT_FLOAT_EQ(actor.Speed, 2.0F);
  EXPECT_TRUE(std::ranges::none_of(*typedOnlyFiles, [](const auto& file)
                                   { return file.RelativePath.filename() == "ModuleMeta.gen.cpp"; }));
  ```
- [x] **Step 2: RED。** 构建 PanguMetaCodeGenTests 与 PanguMetaTool，运行 `ctest --test-dir Build/Windows/Tests/BuildTools/PanguMetaTool -R '^Meta(CodeGen|Cli)[.]' --no-tests=error --output-on-failure`，预期生成/消费断言失败。
- [x] **Step 3: 实现发射器与 CLI。** 公共字段使用 B 的成员指针模板，私有字段只输出到请求的 FieldInfo 表。每头定义 TypeInfo 常量，汇总表只引用 target 常量及 RTTI；使用 constinit/constexpr 初始化，无注册全局构造。QuoteCppString 对引号/反斜线及控制字节转义（控制字节用固定三位八进制），保留合法 UTF-8；元数据内 C++ 符号引用加全局限定，符号转义保持单射。不得因类型打印或生成实现引入机器绝对路径/宿主地址；用户显式语义字符串保持已校验字节。
- [x] **Step 4: GREEN。** 重跑消费、字节稳定性/时间戳及匿名成员类型显示测试；验证仅 check 模式没有目标 C++ 输出、缺 HasDescription 的 BorrowTypeInfo 不可用，描述开关通过 E 的私有派生宏正确选择 Definition.Metadata；CLI 退出码0/1/2与诊断约定一致。
- [x] **Step 5: 提交。** `git commit -m "feat: 按消费需求生成 Meta 接口与装配代码"`。

### Task 10: CMake 输入登记、宿主原生依赖与增量恢复

**Files:** Create `cmake/PanguMetaTool.cmake`、`PanguMeta.cmake`、`BuildMetaTool.cmake`；Modify 工具 CMake 导出构建输入及 Tests/CMakeLists.txt；Create `Tests/CMake/PanguMeta/{RegistrationTests,IncrementalTests,FailureRecoveryTests}.cmake` 及 Fixtures/Consumer 项目；Modify Tests/CMake/PanguMeta/CMakeLists.txt。

**Interfaces:** Produces E 的 pangu_meta_headers/consume/module/host_catalog，目标属性 `PANGU_META_INPUTS`、`PANGU_META_MODULE_BINDING`、`PANGU_META_CATALOG`、`PANGU_SOURCE_OWNER`；具体保存方式可用结构化派生文件，属性只是登记权威。本任务只调度 scan，query/catalog 任务在 Task11 发射器接入时启用；不增加长期过渡开关。仅实际启用任务才 bootstrap 工具，绝不把 LLVM imported targets 链进目标图。

- [x] **Step 1: 写失败构建。** `PanguMetaRegistration` 验证唯一所有者、源码登记与输出集合在 Configure 确定、公开生成头首次消费顺序、按头/Module 需求并集；`PanguMetaIncremental` 验证无变化不启动 Tool、不编译/链接，无关 cpp 只重编自身、传递头/参数/Tool 源变化正确失效。`MissingByproductRecovers` 删除某生成头后仅用普通 build 恢复；`PanguMetaFailureRecovery` 有旧产物后引入错属性，必须阻断消费者，修复后成功。

  ```cmake
  RequireText(commandOutput "no work to do")
  RejectText(commandOutput "PanguMetaTool generate")
  ```
- [x] **Step 2: RED。** `ctest --preset windows -R '^PanguMeta(Registration|Incremental|FailureRecovery)$' --no-tests=error --output-on-failure`，先完成所需 SDK/fixture 前置构建；预期登记/依赖行为缺失。
- [x] **Step 3: 实现 E。** 先收集全部登记，再 DEFER 汇总请求；为每 Module 建不依赖自身生成结果的最小 MetaContext.cpp，用实际编译数据库命令和 object 路径选择上下文。固定 OUTPUT=stamp、代码/outputs index 为 BYPRODUCTS，DEPFILE 跟踪原始/传递头；生成依赖显式连到消费者。请求以 JSON 原语正确转义路径，不复制 compile flags 清单代替数据库。
- [x] **Step 4: GREEN。** 重跑三个系统测试及 Task 1 切换测试；核对 SDK/preflight、工具生产/测试缓存分离，同一宿主工具缓存原生构建串行，普通 no-op 不执行检查脚本。input 删除后重配只清理自身旧输出，include/flag 变化即使内容未变也不错误重链接；实际编译器头依赖仍可使需要的 cpp 重编。
- [x] **Step 5: 提交。** `git commit -m "feat: 将 Meta 生成接入原生目标依赖与增量构建"`。

### Task 11: 迁移实际 Query/Catalog 与公共身份符号

**Files:** Create 工具 `CodeGen/{QueryEmitter.h,QueryEmitter.cpp,CatalogEmitter.h,CatalogEmitter.cpp}`；Modify 工具 CodeGen/Generate.cpp、Driver/RunGeneration.cpp 及其测试、`cmake/Platform/Common/Module.cmake`、根 CMakeLists.txt、各 Runtime/Tests/Samples/Plugin 的 pangu_export_module_query 调用点；Modify `Tests/CMake/SourcePluginProject/MyGame/CMakeLists.txt` 及 Source/GameCatalog.cpp、EditorCatalog.cpp 的消费方式；Modify Samples/MinimalHost/CMakeLists.txt；在确认无旧调用后 Delete `cmake/ModuleQuery.cpp.in`，手写 Catalog 仅在等效生成测试取代其原覆盖后移除。

**Interfaces:** Consumes E；现有 Query C++ 名称、原生 PanguModuleQueryV1、Host 消费集合与 ModuleId 值保持。所有新增 ID/ID_HEADER 引用现有公共常量；确需公开常量的 Module 将 Definition 引用迁到同一来源。

- [x] **Step 1: 写失败回归。** 扩充 `SourcePluginStaticCatalog`、`SourcePluginSharedDeploy` 及 `PanguMetaRegistration`：Game 只含 Gameplay/Physics，Editor 只含 EditorExtras/Physics；Monolithic 真正引用对应 Query，Modular 不隐式导入业务 DLL；每个原生包装恰好一个定义。`MetadataDefinitionUsesSharedIdentity` 对提供者 Owner 与 Query Definition.Id 做真实消费断言。

  ```cpp
  EXPECT_EQ(provider.Owner, definition.Descriptor.Id);
  EXPECT_TRUE(provider.Owner.IsValid());
  ```
- [x] **Step 2: RED。** 构建对应 fixture 并运行三组，预期仍有手写 Catalog/旧模板或未保存 Monolithic Query 登记导致失败；不要因所有者根路径变化删除旧断言。
- [x] **Step 3: 迁移登记。** pangu_export_module_query 两种模式均登记信息；逐目标切换生产者，所有旧源从 target_sources 中撤销后才启用新输出，禁重复入口。ModuleDefinition 仍由业务作者维护；描述启用目标显式引用生成 kModuleMetadata，query-only 目标 Metadata 为空。
- [x] **Step 4: GREEN。** 两模式实际构建、Query 符号检查、部署副本哈希、静态归档最终提取与无变化构建通过；确认 Root 新启用 Query/Catalog 需要明确 SDK 配置，完整 LLVM 依赖只在宿主工具。更新测试支持对新 Bin/Lib/Export 所属根的寻址，保留 Project 锁与归属测试。
- [x] **Step 5: 提交。** `git commit -m "feat: 以 PanguMetaTool 生成 Module Query 与 Host Catalog"`。

### Task 12: 真实生成 Module、对象消费与物理卸载

**Files:** Create `Tests/Meta/CMakeLists.txt`、`MetaIntegrationTests.cpp`、`MetaUnloadTests.cpp`、`MetaLifetimeFixture.cpp`、`MetaLifetimeTests.cmake`、`Fixtures/{MetaActor.h,MetaModule.h,MetaModule.cpp,MetaService.h,MetaService.cpp,CMakeLists.txt}`、`Benchmarks/MetaAccessBenchmarks.cpp`；Modify Tests/CMakeLists.txt、Samples/MinimalHost/MinimalService.h、MinimalService.cpp、Main.cpp 及样例 README/CMake。

**Interfaces:** Fixture ModuleId 为十六进制 `e1000000000000000000000000000001`（无 Service）、`e1000000000000000000000000000002`（Context Service）；显示名 MetaFixture/MetaServiceFixture。Actor 无 TypeId，StableActor 的 TypeId=`e2000000000000000000000000000001`。公共身份以显式字节 constexpr ModuleId 定义，用 FormatModuleId 与上述字符串核对；不新增运行期 ID 分配器。

- [x] **Step 1: 写失败闭环。** `MetaIntegration.NoServiceDescription` 用正式生成 Traits/Provider、EngineHost::Start 和 BorrowTypeInfo 成功枚举；`GeneratedFieldsChangeRealObject` 验证 bool/int/float/enum/对象字段，const/ReadOnly/Private 描述及直接代码结果。`MetaUnload.MetadataBorrowGatesAndPhysicalUnload` 保留描述借用后 BeginShutdown，断言新借用失败/Finish 仍 Busy；释放后 Finish 成功，并在真实卸载 marker 后检查拥有型复制诊断。实例用例经具名 ScopeAccess→ScopeBorrow 实际访问服务中的 Actor。

  ```cpp
  auto info = pg::meta::BorrowTypeInfo<MetaActor>(host);
  ASSERT_TRUE(info);
  const std::string savedName(info->Get()->Name);
  const auto begun = host.BeginShutdown(report);
  EXPECT_TRUE(begun || begun.error() == pg::module::MakeModuleError(pg::module::ModuleError::kBusy));
  EXPECT_EQ(host.GetState(), pg::engine::HostState::kClosing);
  const auto finish = host.TryFinishShutdown(report);
  ASSERT_FALSE(finish);
  EXPECT_EQ(finish.error(), pg::module::MakeModuleError(pg::module::ModuleError::kBusy));
  ```
- [x] **Step 2: RED。** 构建新目标 PanguMetaIntegrationTests/PanguMetaLifetimeFixture，`ctest --preset windows-precommit -R '^(PanguMetaIntegrationTests[.](MetaIntegration|MetaUnload)[.]|MetaLifetime$)' --no-tests=error --output-on-failure`；错误 Provider 负例必须实际走 C 的接纳/查询保护，不用旧人工表冒充生成测试。
- [x] **Step 3: 接通夹具与样例。** Modular 通过 pangu_meta_consume 使用公共值类型/生成头，候选路径与构建依赖显式生成，不直接导入业务描述数据或业务方法；用于物理卸载测试的业务方法保持头内定义。公共值类型按现有夹具在 POSIX 显式 default visibility，Windows 不对这类非多态数据类标 class dllimport；ELF Host 按现有 ENABLE_EXPORTS 契约配置。允许私有观察 marker，但 Pin 和描述获取必须走生产高层入口；元数据表仅要求模块内部链接可见，不无条件扩大原生数据导出表。
- [x] **Step 4: GREEN。** 两模式运行上述测试与 MinimalHostSmoke，检查 moved-from/错误类型/无 Provider/未 Active/Closing/Busy 重入和批量遍历不重复 Pin。新增 opt-in PanguMetaAccessBenchmarks（既有 GoogleBenchmark 边界）对直接访问/生成访问/描述冷查询建立基线，默认 OFF；测量不移出必要 Gate 以美化结果。
- [x] **Step 5: 提交。** `git commit -m "test: 验收生成元数据消费与安全卸载闭环"`。

### Task 13: 三桌面、ARM64 移动最终验收与收尾

**Files:** Create `Tests/CMake/PanguMeta/CrossCompileTests.cmake`、`Fixtures/Mobile/{CMakeLists.txt,Context.h,Consume.cpp}`、`Tests/Meta/README.md`；Modify Tests/README.md、BuildTools/README.md、wiki/core/reflection-meta.md、wiki/foundation/build-system.md、toolchain.md、wiki/core/module-api.md、engine-host.md；Create `docs/superpowers/plans/2026-11-09-pangumeta-tool-validation.md`，更新本计划状态与关键提交。

**Interfaces:** Mobile 消费同一正式工具/生成协议，不运行目标 GTest；新增脚本参数固定 `PANGU_ENGINE_SOURCE_DIR`、`PANGU_TEST_ROOT`、`PANGU_TARGET_PRESET`（android-arm64 或 ios-arm64）、`PANGU_LINK_MODE`、`PANGU_META_HOST_TOOL`、`PANGU_META_SDK_CONFIG`、`PANGU_ENGINE_ALLOW_LOCAL_OVERRIDE`。各 Root 构建与测试串行。

- [x] **Step 1: 写最终契约用例。** Mobile Context.h 的字段 Key/属性常量使用 `sizeof(void*)`、`__aarch64__` 和 Android API/iOS deployment，目标消费 static_assert 核对；检查真正目标文件架构、SDK 及动态依赖，不带 LLVM/Clang/宿主 STL。正式 Graph fixtures 覆盖 A=17/B=29 的往返、切换失败恢复、末次 no-op 和跨配置 Request 失效；whole-branch review M4 收尾另补 A=17/B=17 的单配置同参数 A→B→直接 A，核对 CurrentProject、request 来源/数据库和成功 stamp。该补充不归入先前 Full 轮次。

  ```cpp
  static_assert(sizeof(void*) == 8);
  static_assert(__aarch64__ == 1);
  ```
- [x] **Step 2: 跑定向矩阵。** 三桌面正式 preset分别配置/构建新目标，运行工具/根测试组；Android/iOS 正式根与 Mobile fixture 两模式最终链接。命令模板见下表；环境/SDK 缺失单独记录，不算 RED 或通过，Android 仅 ARM64。
- [x] **Step 3: 完整验收。** 固定已提交 SHA、子模块状态、override=OFF，三桌面各按 Monolithic→Modular 做正式 build/full CTest，保留既有 ModuleRegression 与 Project 测试。工具内层和根调度结果分别记录；不重复调度同一工具组来拼计数。format/tidy 使用真实数据库；记录直接消费/生成/描述与首次/增量/no-op 证据。
- [x] **Step 4: 恢复与归档。** 桌面恢复 Modular、Samples/Benchmarks OFF、正式测试开关按既有默认；Android/iOS 恢复 Monolithic/BUILD_TESTING OFF；只补恢复所需定向验证。归档被测 tree/SHA、命令/退出码、SDK来源、真实覆盖、限制与性能；测试永久保留。本任务保留计划 workspace，controller 在独立评审/全分支评审通过且无活跃依赖后精确清理。
- [x] **Step 5: 提交。** `git commit -m "docs: 归档 PanguMetaTool 正式验收与实现状态"`；相同已测 tree 的分支整合按 AGENTS.md 复用全量结果。

## 验证命令与状态更新约定

定向测试前先构建各步骤列出的真实目标；Task4 的机器 SDK 路径从通过预检的 PanguMetaSdk.cmake 读取。Windows 示例 `cmake --preset windows -DPANGU_META_SDK_CONFIG:FILEPATH=<该文件绝对路径>`；Shell 中路径参数按本机规则作为单独参数引用，不将尖括号原样执行。

每次代码步骤后对实际改动文件执行 `clang-format --dry-run --Werror <该任务的.h/.cpp>`；`clang-tidy -p <实际编译数据库目录> <受影响.cpp>`，Root 目标数据库为 Build/Windows，宿主测试为 Build/Windows/Tests/BuildTools/PanguMetaTool，嵌套 Project 则使用对应夹具打印的 binary 目录。头文件通过其真实测试 TU 检查，不构造会掩盖实际 flags 的假数据库。

| 场景 | 正式命令/明确通过条件 |
| --- | --- |
| Windows 工具定向 | 构建 `Build/Windows/Tests/BuildTools/PanguMetaTool` 的对应目标后，`ctest --test-dir Build/Windows/Tests/BuildTools/PanguMetaTool -R '^Meta' --no-tests=error --output-on-failure`；所有实际匹配项原生码0 |
| Windows 根矩阵 | `cmake --preset windows -DPANGU_LINK_MODE=Monolithic -DPANGU_BUILD_SAMPLES=ON` → `cmake --build --preset windows-full` → `ctest --preset windows-full --output-on-failure`；再以 Modular 重复本轮必要矩阵，SDK配置来自 Task4 |
| Linux/macOS | 在约定共享源码路径将 preset 替为 linux/macos；WSL 用 Ubuntu，Mac 用 BatchMode SSH；SDK/工具缓存和日志位于各自本地 BuildRoot，不通过 /mnt/d 混用Linux树 |
| Android ARM64 | `cmake --preset android-arm64 -DPANGU_LINK_MODE=Monolithic -DBUILD_TESTING=OFF` → `cmake --build --preset android-arm64`；调用 CrossCompileTests.cmake（TARGET_PRESET=android-arm64），再 Modular 重复；真实 AArch64 ELF/最终链接/依赖检查通过 |
| iOS Device ARM64 | Mac 共享源码下 `cmake --preset ios-arm64 -DPANGU_LINK_MODE=Monolithic -DBUILD_TESTING=OFF` → `cmake --build --preset ios-arm64`；相应脚本 TARGET_PRESET=ios-arm64，再 Modular 重复；真实 ARM64/iOS Mach-O 最终链接及依赖检查通过 |

移动脚本的完整参数形态：`cmake -DPANGU_ENGINE_SOURCE_DIR=<共享源码绝对路径> -DPANGU_TEST_ROOT=<本平台BuildRoot>/Tests/MetaMobile -DPANGU_TARGET_PRESET=android-arm64 -DPANGU_LINK_MODE=Monolithic -DPANGU_META_HOST_TOOL=<已验证宿主exe> -DPANGU_META_SDK_CONFIG=<宿主SDK配置文件> -DPANGU_ENGINE_ALLOW_LOCAL_OVERRIDE=OFF -P Tests/CMake/PanguMeta/CrossCompileTests.cmake`。iOS 仅替换 preset、平台本地测试根及宿主路径；模式切为 Modular 另做最终链接，不增加其他 Android ABI。

所有测试成功均要求实际匹配非空、预期诊断与退出码一致；负例崩溃不算预期失败。未完成任务保留 checkbox，Task13 完成时将每任务关键提交、正式验收摘要和遗留限制回写。原计划代码块保留设计意图；当前实现与实跑范围以正式 Tests 和验收记录为准。

## 编制阶段自审与执行交接（历史记录）

- 规格 §1–§3 / Q4、Q9–Q16：Task2/5/6/7/9；§4 / Q2、Q6、Q7、Q10、Q11、Q21、Q24：Task2/3/9/12；§5：Task9/11。
- 规格 §6 / Q1、Q3、Q8、Q16、Q19、Q20、Q23：Task1/5/6/8/10/11；§7 / Q5、Q13、Q14、Q22：Task4/6/7/10；§8 / Q17、Q18：Task12/13。五项 Review Focus 均已分配正式测试。
- 已自行检查任务顺序、共享类型/签名、正式测试归属及比例：ScanResult 单独传递构建依赖；提供者/Traits 先于生成消费；Query/Catalog 发射与迁移在同一后续任务；新 SDK/Root 接线的前置条件不能冒充 RED。这里只记录计划自审，不冒充已执行的编译/测试结果。
- 执行前审阅本计划的实现选择，尤其 A 的共用编译依赖与 C 的可选 Metadata 提供者字段；它们落实已确认行为，不新增全局 Registry、磁盘IR或其他Android架构。该段为编制阶段约定；后续已按授权完成实施，实际裁决与结果见正式验收。

## 实施状态（已完成）

Tasks 1–13 已完成并分别评审；关键提交、剩余非阻塞改进与完整实施裁决已归档于 [正式验收](2026-11-09-pangumeta-tool-validation.md)。Task13 实现提交 `e5c349c`，夹具修复 `28b086c`，最终文档与测试补强 `42067dc`；完整矩阵按真实 SHA/轮次与原生 exit 归档，R3 补收据及 Unix Mono 复用边界明确记录。

验收摘要：三桌面双模式 Full 均有成功记录，Windows 172/164、Linux 171/163、macOS 171/163；两移动双模式实际最终链接/对象审计通过，无设备运行声明。整分支评审无 Critical/Important，最终 M1–M4 修复及一次限定复审通过；收尾补验 metadata 16/16、CodeGen 内层 7/7、同参数 ProjectSwitch 1/1，format/tidy 原生 exit0。M5/M6 保留为非阻塞测试改进，既有告警与首版支持限制见正式验收。分支保留 `docs/pangumeta-standalone`；临时清理仅限本计划 `.superpowers/sdd/2026-11-09-pangumeta-tool/`，长期 Tests、各平台 BuildRoot 和 SDK 保留。
