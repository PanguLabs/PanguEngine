# Pangu ModuleDescriptor & Registration API v1

> 状态：**Partially Frozen**。身份、依赖、注册、Scope、代码驻留和操作后置条件是现行行为契约；Public C++ 签名、布局、ABI 和长期封装 API 未冻结。
> 更新：2026-11-07
> 决策：[ADR-0015：统一链接模式与 ModuleManager](../decisions/0015-unified-link-modes-module-manager.md)，保留 ADR-0010/0011 未被替代的行为；源码交付边界沿用 ADR-0014。
> 代码状态：Task 1–7 已实现统一模式、ModuleManager、原生 Loader、Pin 和动态 Scope。实际平台/模式、首轮与重跑、限制见[正式验收记录](../../docs/superpowers/plans/2026-11-06-module-manager-link-modes-validation.md)，不从设计状态推断平台已通过。

## 1. 设计目标与边界

同一 CMake 目标图用 `PANGU_LINK_MODE=Monolithic|Modular` 统一全部 Pangu Module 为 STATIC 或 SHARED；一个 Module 一个真实 Library Target、一个主要库产物。桌面默认 Modular，Android/iOS 默认 Monolithic。ThirdParty、Host、BuildTools 按自身用途建普通目标。不建立 BinaryImage、统一 IModule/IService、反射注册表或通用 Loader Manager。

配套源码 Module 使用 C++23、无异常、保留 RTTI 和受控 ABI。Plugin 是发现/启用与分发单元，Module 是代码 Owner，Host/Context/World 是实例 Owner。没有独立预编译 Pangu Plugin 接纳、ABI Preflight、Hot Reload 或状态迁移；OS Load 可能在 Query 前运行初始化器，校验不构成安全沙箱，业务不得靠全局构造启动。

## 2. 身份与描述数据

`ModuleId` 明确分配并持久保留为 128 位身份，全零无效，按完整 16 字节比较；文本为数组字节顺序的 32 个小写十六进制字符。身份与名称、路径、PluginId、RTTI、地址、加载序号、hash 无关；改名和重建不更换身份。`ModuleVersion` 是三个 uint32_t 非负整数，0.0.0 合法；v1 精确匹配，不做范围或隐含 Major 兼容。

`Source/Runtime/Foundation/Public/ModuleIdentity.h` 和 `ModuleDescriptor.h` 是纯协议头，由 Foundation 的 PUBLIC 依赖传播；公开 include 仍为 `<ModuleIdentity.h>`、`<ModuleDescriptor.h>`，没有旧目录转发头。`ModuleDefinition` 只含 Descriptor 和非空 RegisterModule；`LinkedStaticModules` 已移除。Query、返回定义和注册函数均非空；无业务能力仍提供显式成功的空注册函数。

Descriptor 借用依赖 span 和诊断 string_view。Manager 在真实代码持有期间校验并深复制必要字段到拥有型 `OwnedModuleDescriptor`；`ModuleSnapshot` 自有 Descriptor、Bindings、State、Resident，不向调用方发布可跨增量修改保留的记录地址。快照和报告的复制/销毁不得依赖已卸载业务代码。

### 2.1 依赖选择与阶段要求

每个 Manager 域内一个身份至多一个生效版本，常驻和待清理记录仍占身份/预算。Required 缺失或版本不符拒绝；Optional 即使候选存在且版本相同，也仅按显式选择绑定，缺失/不符/未选择分别保留结构化原因。明确要求绑定失败不能静默降级。

有效依赖在批次接纳时固定；新增候选不补绑旧记录，变更绑定须先退出依赖方再重新 Load。拒绝重复依赖、自依赖、有效图环及 CleanupPending/Unloaded Provider。Register 要求依赖 Registered/Active；Activate 要求 Active。清理保留既有依赖能力，不重新开放 Provider 的业务闸门。

## 3. STATIC Catalog

Monolithic 的 Catalog 是最终 Host 的显式装配源，直接引用原生 Query，保留真实归档中的入口；不创建第二个 Module Library，不依赖全局业务构造或 whole-archive。静态候选通过 `StaticModuleCandidate{Query}` 进入普通 Load；归档没有独立 OS Unload。

Modular 的每个固定 Query 只返回本 Module。禁止 SHARED 内嵌另一个逻辑 STATIC Module、同库 Catalog、共享承载账本和同图双变体。普通 ThirdParty STATIC 依赖仍合法，不因此获得 Module 身份。已链接的宿主常驻 Module 通过 Create 的显式 Query 集合登记，不作为普通动态候选重复 OS Load。

## 4. SHARED Discovery：固定符号定位 + 原生 C++23 Query

`SharedModuleCandidate` 持有 ExpectedId 与拥有型 filesystem::path。Manager 取得真实 OS 持有，在 Windows 用 LoadLibrary/GetProcAddress，在 POSIX 用 dlopen/dlsym 查找 `PanguModuleQueryV1`，转调同一种原生 C++23 Query。C linkage 只用于稳定符号定位，不是 C POD 数据交换或强制 C Bootstrap。

`pangu_export_module_query(TARGET target QUERY function HEADER header)` 仅为 SHARED 编译包装；公共 C++ 符号经生成 export header 显式导出，保留 RTTI。OS 隐式装入导入依赖不等于 Manager 已接纳/注册该依赖，仍须显式候选和运行期声明。Query 只提供定义，不注册、不激活、不递归发现未知模块。

POSIX 使用 `RTLD_NOW | RTLD_GLOBAL`，使受控 C++ ABI 下的公开 RTTI 可在独立装载组间合并；保持逐 Module 最小显式公共导出和私有隐藏。LOCAL 的实现选择在两桌面真实回归中无法满足合法类型值匹配，已由 Task 8 替代。代价是公开符号进入进程作用域，必须约束 ODR、名称冲突和符号插入；这不是插件隔离或外部 ABI 兼容保证。

Load 对显式候选整批接纳：检查输入、预算/已知身份，取得原生持有并 Query、深复制、复核协议/预期身份/入口/依赖后整体提交；返回本批依赖优先句柄。任一失败不交付部分句柄、不执行业务注册；该原子性不包含 Register/Activate，也不保证撤销 DLL 初始化副作用。

失败批次未完成释放时保留内部占位、持有、原错及有界私有清理组，Poll/Shutdown 可以继续。依赖描述不完整时保守阻止潜在 Provider 清理，旧 Active 业务闸门和固定绑定不变，全部真实释放后解除屏障。代价是部分无关卸载可能暂时 InUse/Busy，不能永久全局阻塞。

仅对从未成功接纳、从未 Register/Activate 的拒绝批次，先释放无可信完整描述项，可信无环图按逆拓扑；元数据环没有合法业务拓扑，可逐项释放 Manager 自身 OS 持有。首次释放失败即停并保留整批占位/剩余持有及潜在 Provider 屏障；真实原生导入持有仍由 OS 维持。此例外不得用于有效业务记录，也不承诺非法元数据图的业务析构顺序。

## 5. 注册与 Scope 装配

RegistrationContext 是带 Module Owner 的声明事务，不是全局 ServiceLocator。注册只暂存、验证并提交可重复装配的拥有型工厂；每次装载至多注册一次，不直接启动全部 Scope。具体接口为 RegistrationScope、ScopeFactory、ScopeBuildContext、ScopeAccess/ScopeBorrow；完整 Contract Provider 选择与业务 Registry API 仍属后续。

Concrete Service 可由 Owner 强类型持有/注入，不强迫注册。Host/Context/World 表示 Scope 种类，ScopeHandle 标识具体 Owner；声明与实例分开持有。Module 初次 Activate 装配全部已有 Ready Scope，全部成功才开放 Module 闸门；之后 CreateScope 只装配当前 Active Module，不重跑 Register。新 Scope 失败只回滚本次，已有 Scope 可继续；初次 Activate 失败则清理该 Module 的全部初次装配。

资源先追踪后 Start，失败也需停止/轮询后匹配销毁。工厂可仅移动，但必须支持多次装配；实例和可变配置按 Scope 独立。显式 ScopeDependency 仅允许当前 Scope 或祖先，禁止跨 Host/Context、反向寿命依赖及未 Ready Provider。Module 与具体资源合并约束必须无环，不能仅分别检查两张图。

### 5.1 强类型与 RTTI

内部类型擦除由 Own<T>/适配器从实际 T 取得 RTTI，Dependency<T> 在代码和对象受保护时用 `std::type_info::operator==` 比较值，匹配后才形成指针；只支持精确类型，不隐式继承转换，不移除接口 const 保护。失配返回 kTypeMismatch。禁止 RTTI 地址、仅 name/hash、人工持久 TypeId/trait 代替检查。Monolithic/Modular 使用同一规则。

交付的 ScopeAccess<T> 热路径只做闸门与计数，不重复查询 RTTI。业务 DLL 的 type_info/type_index、名字视图、对象/Factory/捕获/Deleter 和回调必须在卸载前解除或销毁。控制块、闸门与 shared_ptr 最终释放入口在宿主常驻 Runtime 分配/实现，残留 ScopeAccess 可在业务 DLL 真正卸载后复制、TryBorrow 失败及最终释放，不读取其 RTTI 或执行其释放代码。

### 5.2 已有资源的精确只读访问

内部 `ScopeResourceAccess::Acquire<T>` 接收 Manager 与包含 ScopeHandle、Owner、DeclarationId、Kind 的查询，仅访问已交付 Ready Scope 中的对应活资源。错误优先级：Manager 操作中 Busy；无效/关闭 Manager InvalidState；Kind 不符 InvalidScope；Domain/槽位/generation 不符 ScopeNotFound；未交付或 Closing Scope NotReady；无资源/Owner 错/纯元数据 NotFound；Module 或资源未 Ready 则 NotReady；RTTI 值不符 TypeMismatch。

控制块只有校验成功后才能包装为 ScopeAccess，不提供公开 unchecked/adopt 入口。查询不执行用户代码、不缓存 Manager 地址、不搜索祖先；新令牌只持常驻控制块，不保存业务 RTTI。上层业务具名入口与 Ready 门槛见 [EngineHost](engine-host.md) 和 [ADR-0016](../decisions/0016-engine-host-lifecycle.md)。

## 6. ModuleManager 操作与清理

Module 层公共协调器为 `ModuleManager`，上层由独立 `EngineHost` 编排，旧 StaticModuleHost 已删除。Manager 仅移动，内部 State 地址稳定；管理操作和查询由调用方串行协调，回调不得移动/销毁 Manager 或保留借用 Context；同 Manager 重入在副作用前 Busy。业务 Pin/Borrow 支持并发接纳、释放和关闭。

`RegistrationScope` 声明注册域必须比 Manager 活得更久；Shutdown 撤销本 Manager 的声明，撤销也使原 `Declarations()` 借用视图失效。需要跨清理保留诊断时复制拥有型快照。

ModuleHandle 含 Domain、Slot、Generation，零值无效；代际从 1 开始、耗尽不回绕。旧域/代际不能指向新记录。ModuleState 为 Loaded、Registered、Active、CleanupPending、Unloaded；没有 RegistrationFailed 兼容别名。Find/Inspect/Modules/RegistrationOrder 是串行冷路径拥有型查询，不复活关闭记录。

### 6.1 操作后置条件

| 操作 | 后置条件与继续责任 |
| --- | --- |
| Create(declarations, residentQueries, selections, report) | 必须含 Foundation/Module 两基础根；接纳常驻集合后仍 Loaded，宿主自行按依赖顺序 Register/Activate，无重复 OS Load |
| Load(candidates, selections, report) | 非空显式批次整体交付，单次失败无部分成功句柄；Modular 普通候选必须 SHARED，Monolithic 必须 STATIC |
| Register/Activate(handle, report) | 前置拒绝不改状态；执行失败保留原错，可卸载记录进入单记录清理，常驻记录请求整体 Shutdown |
| BeginUnload/TryFinishUnload(handle, report) | 常驻一律拒绝；仍有有效依赖方先拒绝。可卸载记录关闭新业务，逐步清理，成功才失效句柄/释放身份 |
| CreateScope/BeginCloseScope/TryFinishCloseScope | 新 Scope 局部事务；独立关闭子先父，父有子记录不能完成关闭，根只能随整体 Shutdown 关闭 |
| PollCleanup(report) | 推进失败批次、未交付 Scope、待清理记录和已请求 Shutdown；不独立确认常驻退出 |
| BeginShutdown/TryFinishShutdown(report) | 关闭新管理/业务接纳，幂等推进；TryFinishShutdown 成功统一使常驻句柄失效，未完成保持 Manager/注册域存活 |

Foundation 固定 ID `9f80b25853204dfba45ae9d87c9a1201`，Module Runtime 固定 ID `c0e3457b71fb49e39d0c8c0b232ea402`，版本均 1.0.0、协议 1，后者 Required 前者。BuiltinResidentQueries 返回两原生 Query。常驻 Register/Activate **执行失败**在同次操作请求整体关闭，报告 CleanupOwner=kManagerShutdown；前置拒绝不触发。原始报告销毁后仍可 Poll/Finish 读取原错，普通 TryFinishUnload 仍拒绝。资源排空期间保留常驻诊断句柄，最终确认不释放宿主原生持有，也不能原地重启同一 Manager。

### 6.2 代码驻留、清理顺序与原生成功含义

ModuleCodePin 是仅移动代码保护，TryPinForBusiness 仅在 Active 且业务闸门开放时成功；ScopeBorrow 保护实例及必要代码，ScopeAccess 本身不保活实例。槽位发布/回收与 Pin 解析共享 AdmissionMutex，回收关闭旧闸门；锁内不执行用户回调或 OS 装卸。管理器基本持有、外部 Pin、Borrow 和内部资源依赖分开计数。

整体 Shutdown 按**实际有效资源 DAG、同 Module 跨 Scope 后代资源顺序**逐项 Stop/Poll/Destroy、退休 Factory，随后等待对应外部 Pin、释放该记录原生持有。依赖方原生释放未确认前不销毁 Provider 资源；不能先清空所有 Scope，也不能等待整个闭包的内部 Borrow 全归零才开始销毁。常驻 Consumer 资源排空且接纳中计数与外部 Pin 均归零后可解除其有效依赖边，允许业务 Provider 退出，但常驻记录最后统一确认。Scope 记录回收仍遵守 Owner 句柄契约。

Busy、停止错误或 OS Release 错误保留必要资源、依赖、记录、持有与原错，可继续重试；没有 ForceUnload 或超时强毁。最后 Pin/Borrow Release 只更新保护，不在任意业务线程执行 OS Unload。正常调用显式推进 Shutdown，析构最多同步尝试一次，未完成则进入所有配置有效的终止路径。

TryFinishUnload 成功表示受管生命周期完成、Manager 自己的原生释放调用成功、身份和句柄失效；系统或额外宿主导入/OS 引用仍可使库驻留。实际物理卸载证据须来自排除额外导入持有的可卸载夹具及库级事件；不承诺任意共享 Runtime 从进程移除。

### 6.3 报告、错误与预算

ModuleOperationReport 是无业务借用的值报告，区分 Progress、PrimaryError、OriginalError、CleanupError、Module/RelatedModule/Scope/Declaration/Handle 和 CleanupOwner；不能从 Error Domain/Code 推断是否执行回调。原始 Load/Register/Activate/Scope 错误跨 Poll 保留，清理成功不把原操作改写成功，不长期借用调用方 report。Windows 原生错误可保留 GetLastError 数值；POSIX dlerror 即时消费，公共 Error 只保留确定 ModuleError，不保存借用文本，诊断粒度较粗。

CleanupOwner 表示继续清理的协调责任，调用方仍依据原操作与 ModuleHandle/ScopeHandle 选择 Finish 入口。未交付的失败 Scope 仍待清理时为 kManagerCleanup，只能由 Manager 的 PollCleanup/Shutdown 继续；同步回滚完成或前置拒绝为 kNone。已交付 Scope 的显式关闭待完成时复用 kCallerUnload，完成为 kNone；整体 Shutdown 进行中的待清理报告为 kManagerShutdown，不因显式关闭单个 Scope 转回调用方责任。

最多 256 Module/Manager（常驻及待清理计入）、每 Module 64 依赖、每描述文本 256 字节、每次注册 256 声明、注册域 4096 声明、256 Scope（含根）、每工厂 64 Scope 依赖。OOM 沿用无异常终止政策；普通装载/校验错误必须可恢复。

## 7. 验收与性能边界

正式回归在 Tests，按 boundary/area/tier 接线，旧有效 Mono 覆盖保留且由 Modular 的 ModuleRegression 独立子图执行。真实 DLL 覆盖跨库 RTTI/STL、私有类型、失败批次、关闭故障/重入、外部 Pin/Borrow、并发接纳、卸载后凭据和 Busy 析构。Project 验证父模式、消费集合、动态部署与启动、无变化构建和模式往返，不以 OS 隐式加载冒充接纳。

三桌面须分别完成实际构建/运行、Query/依赖及卸载观察。Android/iOS 两模式必须正式根构建与独立 Project 的 Runtime/业务库和最小宿主最终链接；Android 为 AArch64 ELF 原生宿主 .so，iOS 为 ARM64 Mach-O executable/platform 2。仅归档、空 main 或未消费 Runtime 的 Probe 不足。移动端不执行目标 GTest，不宣称设备 RTTI/卸载、打包、签名或分发可用。

六场景 benchmark 的身份输入、稳态借用与 1/8/32 业务 Catalog 保持；Create/析构计时现在包含两个必要常驻根，不移出计时改善结果，不与旧 STATIC 基线混算。dry run 只证明可运行；Ninja 无变化时不得新增扫描/生成/重链接。

## 8. 冻结范围与后续 API/ABI 门槛

ADR-0015 替代混合类型、同库 Catalog 和常驻执行失败的旧规则，身份/精确版本、显式依赖、声明事务、Scope 局部回滚、所有权和安全清理继续有效。本文叙述现有实现 API，**不冻结 Public C++ 布局、符号 ABI、长期 CMake API 或跨配置二进制兼容性**。未来 PanguMeta 可生成显式 Query/Catalog，但不得改变单库和强类型边界；本实现不依赖 Meta。

EngineHost 最小生命周期已实现；完整 Contract/Registry Service、TaskScheduler/GPU 集成、Hot Reload/状态迁移、安装/发布和移动设备政策仍属后续。历史独立二进制插件 Preflight 已由 ADR-0014 取消，不是当前冻结待办。

## 9. 本轮验证证据与限制

以下保留各阶段当时接口、路径和成绩，已由 [ADR-0015](../decisions/0015-unified-link-modes-module-manager.md) 与上方现行规范替代；旧 STATIC 或局部 Probe 结果不代表本轮 Modular 验收。

### 历史 STATIC Scope 生命周期切片（2026-10-29）

在下述 Descriptor/Catalog 切片之上，新增 `ScopeFactory` / `ScopeBuildContext` / `ScopeAccess<T>` / `ScopeBorrow<T>` 及 STATIC Scope 协调器。工厂声明一次、按具体 Scope 重复装配；Scope/Module 闸门与原始错误由 Host 持有，完成 Start 才发布访问；失败进入局部或 Module CleanupPending，Poll/外部借用未完成时保持资源存活，逆序逐个 Stop/Poll/Destroy。未交付句柄的失败 Scope 由 Host 持续跟踪。

`StaticModuleHost` 新增 Activate、固定 Catalog 项重新 Load、Scope 创建/关闭、受管逻辑卸载及 Shutdown；仅是同次构建 STATIC 实现，不执行 OS 卸载。句柄含 Host Domain/槽位/generation，旧 Scope 或旧访问控制块不会指向重建实例。注册域撤销也会使 `Declarations()` 借用失效；需跨清理持有诊断时复制值快照。

API/布局仍未冻结。当前库的 `std::move_only_function` 可用性不一致，统一使用内部拥有型仅移动 callable 适配；OOM 仍为终止政策。具体合并图门槛、预算、报告、析构契约、实际验收与未验证部分见 [设计](../../docs/superpowers/specs/2026-10-28-module-scope-lifecycle-design.md) 和[计划](../../docs/superpowers/plans/2026-10-28-module-scope-lifecycle.md)。不据此声称完整 Service Registry、动态 CodePin、SHARED 预检、最终 EXE ABI 或五平台设备运行已经验收。

### 历史 STATIC Descriptor/Catalog 切片（2026-10-28）

在原 `PanguModule` STATIC Target 内新增 Descriptor、`StaticModuleHost` 与声明注册事务，规格见 [STATIC Catalog 实施规格](../../docs/superpowers/specs/2026-10-28-module-static-catalog.md)，验收见[实施计划](../../docs/superpowers/plans/2026-10-28-module-static-catalog.md)。具体签名及布局仍未冻结。

- `ModuleDescriptor.h` 定义借用描述、Required/Optional 依赖、原生注册/Query 与扁平 Catalog。Host 在 Create 期间查询一次并深复制字符串和依赖，校验零/重复身份、协议版本、入口、精确依赖版本、显式 Optional 选择及有效图中的环，生成依赖优先顺序。
- `StaticModuleHost` 只接受同一受控二进制的 STATIC Catalog；`ModuleOperationReport` 为无借用的定长值快照。前置拒绝保留状态；回调失败后同步撤回本次声明并保留原始 Error 和不同进度，不允许原记录直接重试。
- `RegistrationContext` 暂存强类型 `DeclarationId`、Owner 和诊断名；`RegistrationScope` 表示一个具体 Owner，持有全部成功声明。同一 Scope 原子发布，声明冲突为粘性失败，同一 Scope 的跨 Host 重入拒绝。它尚不是第 5 节 Service Provider/Contribution/Work 工厂 API。
- 显式限制：256 个 Module、每个 64 个依赖、描述字符串 256 字节、每次 256 条声明、每 Scope 4096 条声明。冷路径使用拥有型标准容器，OOM 不承诺可恢复；操作由调用方外部串行化。
- 正式 [Module 测试](../../Tests/Module/README.md) 覆盖上述行为；两个真实受管 STATIC 夹具经最终 EXE 生成的根引用进行裁剪链接与注册验收。测试源码长期保留，不依赖历史 PoC。

**该阶段边界**：Descriptor 不暴露可手填的 ABI Profile/Fingerprint 字段。旧 STATIC 采集仅属于 2026-10-28 的历史实现，已经由 ADR-0013 退役；当时尚未实现的 STATIC Scope 装配、激活与 Borrow/Busy 排空已由上方 2026-10-29 切片补齐。最终 EXE 的完整 ABI 一致性、Profile 嵌入、Public C++ Contract、完整 ModuleManager/EngineHost、业务 Service Registry、动态 CodePin、TaskScheduler/GPU 排空及 SHARED Loader 仍待实现。本 API 不能用作外部候选放行或动态装载的替代路径；以下早期证据保留其当时范围。

### 早期 Identity 与 PoC 验证（2026-10-22）

### 9.1 正式 Module Identity 代码起步（2026-10-22）

已在 `Source/Runtime/Module/` 创建第一个实际 CMake `STATIC` Target：`PanguModule`，并在仓库根目录建立最小 CMake 入口、将长期回归测试放在 `Tests/Module/`。这只实现已冻结身份/版本规则的一小部分，**不表示 `ModuleDefinition`、`RegistrationContext`、`ModuleManager`、`ModuleCodePin`、ABI Profile 采集或 SHARED Loader 已实现**。

- Public `Source/Runtime/Foundation/Public/ModuleIdentity.h`（公开 include 使用 `#include <ModuleIdentity.h>`）提供 `pg::module::ModuleId`（16 字节强类型，全零无效）和 `ModuleVersion`（三段整数，`constexpr` 比较）；无分配 `FormatModuleId` 支持 `constexpr` 和规范化的 32 字符小写十六进制。
- 内部 `Private/ModuleIdentityParse.h` / `ModuleIdentity.cpp` 严格解析十六进制文本，拒绝长度错误、大写/非法字符与全零 ID；因通用 `pg::Error` Public API 尚未冻结，该领域专属错误**仅保留于内部**，将来的跨模块公共解析接口须遵循 ADR-0007 转换为 `std::expected<T, pg::Error>`。
- CMake 目标使用 C++23、Clang、`-fno-exceptions`，非 Windows 目标明确使用 `libc++`，静态库启用 PIC 以便以后组合进入 SHARED。该根工程尚不是完整引擎构建/部署配置。
- `Tests/Module/ModuleIdentityTests.cpp` 通过 `static_assert` 与 CTest 检查身份类型性质、编译期版本比较与格式化、合法往返及非法文本拒绝。Windows Clang 23.1.0、WSL Ubuntu Clang 23.1.0、macOS MacPorts Clang 23.1.2 **各 1/1 CTest 通过**；Windows `clang-format --dry-run --Werror` 与按编译数据库执行的 `clang-tidy` 无新增诊断。
- 正式构建输出已迁移至仓库根目录 `Build/Windows`、`Build/Linux`、`Build/macOS`（各平台 Module Identity CTest 1/1 通过）；`Build/Android` 的 NDK r30 ARM64 STATIC Module Identity 编译及 `Build/iOS` 的 MacPorts Clang 23 + iPhoneOS SDK ARM64 STATIC Module Identity 编译均已通过（无运行/部署验收）。macOS 使用 `ssh -o BatchMode=yes moozen-macos`，直接从已挂载的 `/Users/moozen/WindowsGit/PanguEngine` 共享源码配置/构建，**不拷贝源码**。
- WSL 的 LLVM libc++ 安装需在配置阶段显式提供实际标准库的两个头文件目录（含 `__config_site`）和库搜索目录；现通过 `cmake/Platform/Linux/Settings.cmake` 基于实际 Clang 安装前缀识别头文件和库，并可通过 `PANGU_LIBCXX_ROOT` 覆盖，不再要求手填 `CMAKE_CXX_FLAGS`。macOS 的 `macos` Preset 显式选择 `clang-scan-deps-mp-23`。先前 `.probe/module-public-api-*` 临时构建与 macOS `/tmp/pangu-module-public-api-*` 副本均已清理，原有其他探针未动。上述结果仅覆盖身份切片，Android/iOS 尚未执行。



### 9.2 既有行为契约探针（非正式源码）

2026-10-22，本地 `.probe/module-api-freeze/` 使用 Windows x64、Clang 23.1.0、MSVC STL、C++23、CMake 4.4.3 / Ninja、禁用异常与 Release 配置完成 **9/9 CTest**。本轮风格修正后，Windows 复验仍为 **9/9**，WSL Ubuntu 使用 Clang 23.1.0 / libc++ 同样完成 **9/9 CTest**。探针被 Git 忽略，不是正式 Runtime 或版本化回归测试；生产实现需将第 7 节要求迁入正式 CMake/CTest。

| 证据 | 覆盖内容 | 不代表 |
| --- | --- | --- |
| 8 项 C++ 状态模型测试 | 身份解析/往返、精确版本与绑定条件、相同 Error 的不同进度、注册/激活失败后清理、Scope 局部回滚与关闭闸门、无句柄装载失败的继续清理 | 生产 ModuleManager、真实线程争用、任务调度、图算法、OOM 或完整报告存储已实现 |
| 1 项真实动态库测试 | 两个 STATIC 库链接进一个 SHARED，单 Query 可达全部定义，跨动态库原生 C++ 注册，空入口/重复 ID 拒绝，宿主复制元数据；测试账本在记录/Pin 未清空前拒绝释放库，清空后 Windows FreeLibrary / Linux dlclose 成功 | 生产 CodePin/原子计数、预检/TOCTOU、完整 ABI Profile 采集或安全加载外部产物已验证 |
| 导出表与裁剪构建 | Windows `llvm-readobj --coff-exports` 仅列出 `PanguModuleQueryV1`，启用 `/OPT:REF` 后 STATIC 查询入口可达 | 导出名/Language Linkage 在全部目标平台已冻结 |

探针声明、导出定义及宿主符号查找均已按 `.clang-tidy` 的 CamelCase 规则统一为 `PanguModuleQueryV1`，并通过上述重建与复验；精确 Public API/ABI 仍未冻结。

复现入口：

```powershell
cmake -S .probe/module-api-freeze -B .probe/module-api-freeze/build-win -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build .probe/module-api-freeze/build-win
ctest --test-dir .probe/module-api-freeze/build-win --output-on-failure
llvm-readobj --coff-exports .probe/module-api-freeze/build-win/probe_shared.dll
```

本轮未完成 macOS/Android/iOS 的此组验收。早期受限会话中 WSL 返回 `E_ACCESSDENIED`；本轮通过获准的 WSL 执行完成了上述 Linux 复验。Android 四 ABI 的工具链 smoke 探针已另行重建，但不能代替本接口验收；macOS/iOS 未运行本探针。测试动态库为同次构建的固定夹具，未实现可信预检；它不降低 ADR-0010 对生产 SHARED Loader 的 Fail Closed 要求。
