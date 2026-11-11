# 统一链接模式与 ModuleManager 设计规格

> 状态：Approved（用户于 2026-11-06 批准）；已完成 grilling、Builder Check、双轴 Review 及 P2 修复的限定复评。本文是已批准设计规格，不是实施计划，不表示接口或平台能力已经实现。
> 日期：2026-11-06
> 实现与验收：见[正式验收记录](../plans/2026-11-06-module-manager-link-modes-validation.md)。设计批准、已实现范围、实际平台结果和最终Review状态分别记录；Public C++ API/ABI未因此冻结。
> 实现基线：`5be751f`。本文独立定义本轮所需契约，不依赖 PanguMetaTool 设计草案或生成器实现。
> 依据：[Module API](../../../wiki/core/module-api.md)、[生命周期](../../../wiki/core/module-registration-lifetime.md)、[所有权](../../../wiki/core/memory-management.md)、[构建](../../../wiki/foundation/build-system.md)、[测试](../../../Tests/README.md)、[源码交付](../../../wiki/decisions/0014-source-plugins-project-build.md)。

## 1. 意图、确认结果与范围

让配套源码构建的 SHARED Module 在宿主持续运行时显式加载、注册、激活、停止和安全卸载。已有 Context/World 可以接纳新 Module；其余 Module 和 Scope 在局部卸载期间继续运行。再次装载从新实例开始，不承诺状态迁移。

用户已确认：

1. 采用运行中显式启停的方案，包含启动加载、退出卸载的使用方式。
2. 将 `StaticModuleHost` 直接演进为统一 `ModuleManager`，迁移现有调用和测试，不保留转发外观。
3. 参考 UE5/O3DE，由整体 Monolithic/Modular 策略决定 Pangu Module 库类型；不允许同一构建配置中的 Pangu Module 部分 STATIC、部分 SHARED。
4. Modular 中一个 Pangu Module 对应一个独立动态库；取消 SHARED 内嵌其它逻辑 STATIC Module 的交付和运行协议。
5. 桌面日常开发默认 Modular；Monolithic 用于整体静态交付。
6. 一个 CMake 目标图统一一种模式；Editor Modular 与 Game Monolithic 分别配置和构建，不在同一图中为 Module 创建双变体。
7. 批量 Load 的原子性限定为本批受管记录的整体接纳；不包含 Register/Activate，也不承诺撤销 DLL 初始化器的副作用。
8. Foundation/Module 补齐正式身份，以宿主常驻方式登记，仍显式 Register/Activate；普通卸载接口拒绝单独卸载，Shutdown 最后完成其逻辑收尾。
9. 管理操作和查询由调用方串行协调；Pin/Borrow 的接纳、释放与业务闸门关闭支持并发。
10. 以既有受控 C++ ABI 和 STL 跨 Module 契约为前提，采用强类型持有/依赖注入，并在内部类型擦除处使用原生 RTTI 值比较；本轮不要求人工持久 TypeId 或关联 trait。
11. 卸载成功表示受管清理完成且 Manager 成功释放自己的 OS 持有，不保证存在其它原生引用时动态库已从进程移除。
12. Shutdown 未完成时保留资源并返回 Busy/具体错误；调用方强行析构仍未完成关闭的 Manager 时终止进程，不提供 ForceUnload。
13. Android/iOS 默认 Monolithic，同时必须完成 Modular 的真实 Runtime/业务 Module 动态库、Query 导出和最小宿主链接验收；设备运行和 APK/IPA 打包后续另行实施。

本轮是一个跨构建与 Runtime 的项目：既要建立两种模式，也要完成真实动态库生命周期。只增加 Loader 而保留生产 Module 硬编码 STATIC，不能视为完成。

**非目标：** 完整 EngineHost/Game/Editor 产品、Hot Reload、状态迁移、新旧代码代际并存、自动扫描动态库、递归自动加载未知依赖、独立预编译 Pangu Plugin/SDK、AbiProfile/Preflight、完整 ServiceRegistry、TaskScheduler/GPU 调度器、PanguMetaTool/Clang Plugin 实现、移动设备运行与商店部署策略。

**不能接受：** 只复制 DLL 就宣称已激活；以裸函数指针代替 CodePin；Busy 时强行释放对象或动态库；复制一套静态 Runtime 到每个 DLL；通过删除回归或将真实库测试改成 Mock 来获得通过。

## 2. 比较过的方案

| 决策 | 备选与代价 | 选定方案及原因 |
| --- | --- | --- |
| 运行场景 | 仅启动加载/退出卸载，组合少，但不能验证运行期间局部装卸 | 用户选择运行中显式启停，保留已有 Scope 和不相关功能 |
| 协调接口 | 保留 STATIC 宿主转发到新核心，保留旧形式但多一层接口 | 用户选择直接迁移为 ModuleManager，公共 API 尚未冻结 |
| 库组合 | 每 Module 自选 STATIC/SHARED，支持混合，但需要同库多记录与共同卸载账本 | 用户要求统一模式，Modular 一 Module 一动态库，简化生命周期与未来替换边界 |
| 类型校验 | 显式 TypeId/trait 可提供持久身份，但新增人工分配与唯一性维护；唯一导出锚点需要逐类型导出归属 | 用户选择强类型接口与内部原生 RTTI 校验，复用保留 RTTI 的既有策略；持久 Schema 身份不在本轮范围 |

UE5 本地 [`TargetLinkType`](../../../../UnrealEngine/Engine/Source/Programs/UnrealBuildTool/Configuration/Rules/TargetRules.cs) 将两种模式分别描述为单二进制链接和独立动态库链接；O3DE [`Monolithic.cmake`](../../../../o3de/cmake/Monolithic.cmake) 统一选择相关库及 Gem 的链接类型。这是本地源码的参考结论，不宣称两项目不存在辅助静态库、平台例外或其它构建机制。Pangu 不照搬 UBT、Gem 分类或 O3DE 的测试禁用政策。

## 3. 统一链接模式

模式、默认方向和配置粒度已经确认；`PANGU_LINK_MODE` 是本设计建议的配置名，不把名称拼写提升为冻结 API。

- 顶层使用一个枚举式配置 `PANGU_LINK_MODE=Monolithic|Modular`，非法值在配置期拒绝；不以多个布尔值表达可冲突状态，不使用全局 `BUILD_SHARED_LIBS` 改变 ThirdParty。
- Windows/Linux/macOS 的默认值为 Modular。Android/iOS 默认 Monolithic，显式选择 Modular 时仍须产出和验证独立动态库。
- Engine 作为 Project 子目录时继承父 Project 已选择的模式，不覆盖父缓存。一个 CMake 目标图使用一种模式；同一图中的 Game/Editor 不对共享的 Module Target 各自选择不同模式。不同模式的产品分别配置、构建，不为同一个 Module 创建 Static/Shared 双 Target。
- Monolithic：所有 Pangu Module Target 为 STATIC，通过最终宿主的显式 Catalog 保留注册入口。最终宿主可以是平台要求的可执行程序或应用共享库，不能把“单二进制”限定为 Windows EXE。
- Modular：所有 Pangu Module Target 为 SHARED；每个目标输出自己的动态库。链接另一个 SHARED Module 表示动态依赖，不将其逻辑身份合并进本库。
- 统一类型包括 `PanguFoundation`、`PanguModule`、Engine/Project 插件 Module 及真正声明为 Pangu Module 的测试目标。普通 ThirdParty、辅助归档、BuildTools、Host 和测试 EXE 按各自用途选择类型。辅助归档不持有独立 ModuleId/Query/生命周期，不得用改名逃避模式规则。
- 继续先用原生 `add_library` 建立唯一 Target，再由既有 Module helper 应用策略并验证类型；模式至库类型的映射集中一次定义。测试中故意构造类型错误必须使用独立的负例配置，不能使有效构建图混用。
- 保留一个 Module 一个主要产物和 `Pangu::`/`ThirdParty::` 命名边界。Windows import library、PDB 仍是辅助产物，产物名称不含平台名。
- 正式 BuildRoot 与平台根 Preset 保持仓库约定。单 BuildRoot 同时只运行一个配置/验收会话；模式切换的配置、重编、重链接、部署和测试发现必须有正式往返回归，不能加载遗留模式产物。其它模式的独立集成夹具可以在该宿主 BuildRoot 的测试子目录配置，不能复制源码或占用其它平台构建树。

### 3.1 常驻 Runtime 与可卸载 Module

当前 Foundation/Module 只有构建目标，尚无各自固定的 ModuleId/Definition/Query。本轮为它们补齐受版本控制的持久身份和原生定义，沿用统一 Module 协议；宿主显式提供已链接常驻 Module 的 Query 集合，Manager 深复制并登记常驻记录，不再次执行 OS Load。常驻是宿主指定的生命周期策略，不新增 Module 分类，也不允许插件通过自行标记来逃避清理。

常驻 Module 仍从 Loaded 开始，由调用方按依赖顺序显式 Register/Activate；代码已被 OS 加载不等于业务 Active。依赖它们的业务 Module 继续遵守 Registered/Active 的阶段要求，不新增隐式启动协议。

统一 SHARED 不等于所有库都由业务 Loader 独立卸载。宿主正常链接的 `PanguModule`、`PanguFoundation` 等运行支持库由宿主链接闭包保持驻留，至少活到 Manager、Pin/Borrow 控制块及报告相关操作全部结束。普通 BeginUnload/TryFinishUnload 拒绝单独卸载常驻记录，重复作为业务候选接纳时拒绝身份冲突；已知常驻身份在取得新 OS 持有前拒绝。Shutdown 先完成业务 Module、工作和 Scope 的清理，再经内部协调完成常驻 Module 的逻辑收尾；不由 Manager 释放宿主的原生链接持有，不能让 Manager 卸载自己的实现。

**常驻 Module 执行失败的例外协议：** Register/Activate 在开始执行回调或装配后失败，失败记录进入 CleanupPending，由 Manager 锁存原始错误并在同一次管理操作内请求整体 Shutdown，关闭新的业务接纳及 Load/Register/Activate/CreateScope 入口。原操作仍返回原始错误，报告明确整体关闭已接管清理；不通过重入公共 BeginShutdown 来安排该内部转换。前置校验拒绝保持原状态，不触发整体 Shutdown。

调用方保持 Manager 与注册域存活，通过 TryFinishShutdown 推进并确认最终收尾；PollCleanup 可推进同一关闭状态，BeginShutdown 可幂等调用，均不得清除原始失败、提前停用清理所需 Provider 或恢复业务入口。普通 BeginUnload/TryFinishUnload 对失败常驻记录仍拒绝，不要求调用方走被禁止的单记录卸载路径。Busy/清理错误保留记录、依赖、原始错误及宿主代码驻留；PollCleanup 即使已完成资源清理，也不单独确认常驻记录退出。常驻记录在整体关闭确认前保留 CleanupPending（尚未轮到清理的记录保持其被关闭的状态）与有效的诊断句柄，由 TryFinishShutdown 成功时统一使常驻句柄失效。成功仅确认关闭完成，不将最初的注册/激活失败改写为成功；同一 Manager 不能重新加载或启动这些记录，需要新 Manager 才能重新登记。

真正测试可卸载的业务 DLL 不直接作为测试 EXE 的链接依赖；测试通过公共契约头和显式候选访问它。业务 DLL 可以正常链接公共 Runtime DLL，不复制静态 Runtime。库类型、宿主导入关系与是否在 Manager 中登记是不同的事实，CMake 中存在一个 SHARED Target 不自动执行运行时注册。

### 3.2 移动端中间产物与最终宿主

| 平台 | Monolithic Module 产物 | 最小最终链接验收 | Modular Module 产物 |
| --- | --- | --- | --- |
| Android | 每 Module 一个 `.a` | 将真实 Module 归档链接进应用原生宿主 `.so` | 每 Module 一个独立 `.so` |
| iOS | 每 Module 一个 `.a` | 将真实 Module 归档链接进 App 形态的最小 Mach-O 可执行文件 | 每 Module 一个独立 `.dylib` |

Android Monolithic 的最终宿主 `.so` 是静态 Module 的聚合承载者，不是又一个 SHARED Pangu Module，不构成混合模式。最小宿主仅用于本轮最终链接验收，不扩大为完整应用、设备运行或 APK/IPA 打包任务；系统和 ThirdParty 的附属库仍按其自身规则接入。

## 4. Module 定义、导出与构建接线

- STATIC/SHARED 继续共用原生 C++23 `ModuleDefinition` 和注册回调，保留版本化 `extern "C"` 名称定位方式，不改为 C POD 交换或 C Bootstrap。
- 本设计建议使用既有提案名 `PanguModuleQueryV1`；调用约定和平台可见性以实际导出、宿主调用及目标链接验收为准，不能只凭转换函数指针通过编译就宣称兼容。
- 移除 `ModuleDefinition::LinkedStaticModules` 及同库多逻辑 Module 的查询/接纳协议。Monolithic 的 Catalog 是宿主装配源，不是额外库；Modular 的每个 Query 只返回本 Module 的定义。
- 公共非模板函数、类型的必要成员、运行控制入口及跨 DLL 类型契约采用逐 Module 的导出/导入策略。允许用途明确的导出宏或生成 export header，它们不是反射标记宏；不依赖 Windows 自动导出全部符号，不依赖 ELF 偶然可见性。
- 定义、Query 包装、Catalog 和导出定义归属原有 Module/Host Target，不增加第二份 Module 库，不引入全局构造注册。
- 本轮可沿用显式 C++ Definition/Query 和最小固定包装模板，生成自动化不依赖 PanguMetaTool 先完成。Plugin.toml 仍只管源码插件身份；构建链接边不是第二份运行期依赖声明。
- Engine standalone、独立 Project、宿主工具与交叉构建继续各自拥有正确的工具链上下文；独立宿主工具属于另一 CMake 图，其模式配置不能被目标平台配置污染。

## 5. ModuleManager：接纳、身份与依赖

一个 Manager 拥有一个 Host Scope、稳定记录存储、声明注册域关联及清理状态。现有 ScopeRuntime/注册事务演进为共用实现，不另建并行的动态生命周期协调器或 BinaryImage 架构层。

### 5.1 候选与接纳

由一个 `Load` 操作接受显式候选集合，返回本次全部 `ModuleHandle` 的拥有型结果；单个 Module 是长度为一的集合，集合不是新的 Plugin/动态库容器。原子性只覆盖本批受管记录的整体接纳，不包含 Register/Activate；DLL 初始化器可能先于 Query 执行，不保证其副作用可回滚。Module 不得用全局构造启动业务。具体 C++ 容器和签名留给实施计划。

- Monolithic 候选来自宿主显式 Catalog 的原生 Query。
- Modular 候选包含预期 ModuleId 和已部署动态库的显式绝对路径；预期身份由调用方的受控 C++ 描述提供，不从文件名猜测。装载前原子预留整批身份，Query 后复核实际身份。
- 普通 Load 只接纳当前模式对应的来源；Modular 不接纳 STATIC 业务候选，不以“兼容模式”绕过构建决策。宿主提供已链接常驻 SHARED Module 的 Query 属于第 3.1 节的常驻登记，不是静态库候选，也不混入业务 DLL 装载批次。
- 对一批候选，先验证输入、预算及已知身份冲突，再取得所需 OS 持有、Query、深复制描述与校验入口/协议/重复身份，最后解析依赖和提交全部记录。任一失败不交付部分成功句柄，也不执行注册回调；原来已运行的 Module/Scope 不因本批失败被撤销或停用。
- Required 的精确版本 Provider 必须已接纳且可用，或位于本批显式集合中；不递归扫描和加载缺失依赖。调用方可一次提交完整集合，或按依赖顺序分批提交。
- Optional 必须由调用方明确选择，未选中保持未绑定并给出原因；显式选择却不能绑定时失败。对本批建立固定绑定并检查有效图无环，新增候选不补绑或修改旧记录。
- 元数据、名字及依赖由 Manager 深复制，回调及候选库内视图只在代码受保护时借用。描述有界，不保存指向 DLL 的诊断字符串或调用方报告引用。
- 单库单 Module 简化了身份预留，不取消失败清理：OS 持有释放未完成时，Manager 保留内部记录、必要身份和原错误，正常 Poll/Shutdown 能观察并推进；调用方未得到句柄也不能使其失管。

### 5.2 句柄与状态

`ModuleHandle` 是不拥有的强类型代际句柄，包含 Manager Domain、槽位和 generation 或等效保护。ModuleId 是持久身份，不能充当某次装载的生命周期句柄。记录位于稳定存储，增量加载不使旧 Scope、依赖和控制块因容器扩容失效。

沿用 Loaded / Registered / Active / CleanupPending / Unloaded 的行为。可卸载 Module 的 Register/Activate 执行失败后进入 CleanupPending，原句柄只能推进收尾，成功 TryFinishUnload 后失效；重新装载获得新代际，旧句柄/访问凭据不能指向新实例。generation/Domain 耗尽拒绝，不回绕。

常驻 Module 的执行失败按第 3.1 节转入整体 Shutdown，由 TryFinishShutdown 确认并统一使常驻句柄失效，不适用单记录 TryFinishUnload 后置条件。关闭期间句柄保留只用于受限诊断与关联报告，不能据此重试 Register/Activate、取得新业务 Pin 或绕过关闭状态。前置拒绝不进入上述失败清理协议。

### 5.3 操作职责

| 操作 | 契约 |
| --- | --- |
| Load | 整批显式接纳、深复制描述、固定依赖，交付新句柄；不注册或激活业务 |
| Register | 依赖先 Registered/Active；每次装载至多一次，暂存、检查、提交可重复工厂；执行失败时，可卸载记录进入单记录清理，常驻记录触发整体 Shutdown |
| Activate | 依赖 Active；向所有已创建且 Ready 的 Host/Context/World 装配；全部成功后开放该 Module 业务入口；执行失败的收尾责任同 Register |
| CreateScope | 按依赖顺序装配当前 Active Module；局部失败只清理新 Scope，不停止已有 Scope |
| BeginUnload | 拒绝常驻记录；可卸载记录仍有需要它的依赖方时前置拒绝，否则关闭新 Pin/Borrow/装配并安排停止；重复请求不重复创建清理工作 |
| TryFinishUnload | 仅推进可卸载记录的排空、逆序销毁、撤销工厂和释放代码持有；未完成返回 Busy/具体错误并保留清理责任，常驻记录不走该入口 |
| PollCleanup | 推进失败装载、未交付 Scope、其它待清理记录及已请求的整体 Shutdown；常驻记录不在此独立确认退出，不要求调用方持有未返回的句柄 |
| BeginShutdown / TryFinishShutdown | 拒绝新管理工作，按 Scope/Module 有效约束收尾；常驻执行失败可内部请求同一关闭流程，BeginShutdown 幂等；TryFinishShutdown 成功确认整体完成并使常驻句柄失效，未完成时必须保持 Manager 与注册域存活 |

不默认级联卸载依赖方。应用先停止依赖方再停止 Provider，Manager 提供结构化依赖拒绝。管理操作与查询由调用方串行协调，同 Manager 重入在副作用前返回 Busy；不承诺多线程同时 Load/Activate。业务 Pin/Borrow 的接纳与释放支持并发关闭。

## 6. 代码驻留与对象寿命

### 6.1 ModuleCodePin

- 新增仅移动 RAII `ModuleCodePin`，只保护代码驻留，不保护某个 Service 实例。
- `TryPinForBusiness(handle)` 仅在 Active 且业务闸门开放时成功。Loaded/Registered/CleanupPending 不发新业务 Pin；与 BeginUnload 之间有明确原子线性化点。
- Manager 的基本装载持有和内部注册/清理权限不作为外部业务 Pin 计数，不因等待自身 Pin 而阻止销毁。
- 持续工作在启动/入队前纳入现有 Scope 资源日志，通过 Start、RequestStop、PollStopped 参与排空。任务/回调/销毁者仍在 DLL 内时必须保持必要代码保护；不要求先引入调度器。
- Host/Borrow 保证实例，Pin 保证代码；外部 Pin、外部 Borrow、内部资源依赖和原生装载引用分别记账，不能混成一个“引用数归零”。

### 6.2 强类型接口与内部 RTTI 校验

当前 `ScopeTypeToken<T>()` 采用模板静态对象地址，只承诺同一二进制。Modular 不能依赖该地址在不同 DLL 中相等。本轮沿用受控 C++ ABI、保留 RTTI 及兼容 STL 跨 Module 传递的既有前提，不增加持久类型身份系统。

**已确认方案：** Concrete Service 由 Owner 强类型持有和显式注入；对需要生命周期保护的访问，公开 `ScopeAccess<T>`/`ScopeBorrow<T>` 等强类型接口。框架仅在内部异构资源存储、工厂装配和 Dependency 解析等类型擦除位置执行原生 RTTI 校验，不强迫业务按 ID 查找任意类型。

类型信息由框架的 Own<T>/资源适配器从实际擦除的 T 生成，不能开放让调用方随意组合类型信息和 void 指针。Dependency<T> 在代码及对象受保护的窗口内，先用 `std::type_info::operator==` 比较期望类型与实际擦除类型，匹配后才形成 T 指针；失配返回明确错误，不执行错误转换。这里只支持接口允许的精确类型匹配，不隐式执行继承转换；顶层 cv/ref 的规范化不得移除访问接口承诺的 const 保护。

比较的是 type_info 值，不是 type_info 对象地址，也不是仅比较 name()/hash_code()。已交付的强类型访问凭据在正常 TryBorrow 热路径只做必要的闸门和寿命保护，不每次重新查询 RTTI；Monolithic/Modular 使用同一匹配规则，不要求业务作者维护人工 TypeId、trait 或类型导出锚点。

RTTI 引用仅由仍受代码保护的内部资源/装配状态借用。业务 DLL 卸载前必须解除其 type_info、type_index、名字视图及相关回调的借用，不能把它们留在卸载后仍存活的控制块中；不通过复制 RTTI 名称或哈希来假装完成类型身份迁移。公共类型仍须满足 ODR、正确导出/导入和已有所有权约束，跨 DLL 实际比较必须在各目标配置下验证。

语义依据：[C++ 标准草案 type_info](https://eel.is/c++draft/type.info) 规定值比较是否表示同一类型；[libc++ 可见性说明](https://libcxx.llvm.org/DesignDocs/VisibilityMacros.html) 体现 RTTI/typeinfo 的符号导出要求。这些资料不代替 Pangu 各平台的实际链接、加载与卸载验收。本轮不建立全局反射注册表；持久 Schema、存档或跨进程所需的 TypeId 留到对应领域设计。

### 6.3 残留访问凭据

`ScopeAccess<T>` 可比实例和业务 DLL 活得更久；它本身不阻止业务 DLL 卸载。卸载后 TryBorrow 返回失效，其复制、销毁和控制块最终释放必须安全。

为此，访问控制块、闸门、引用计数实现和最终释放入口在宿主常驻 Runtime 中分配并实现，不把 DLL 内模板 `make_shared` 生成的销毁器/vtable 留在残留凭据里。资源对象、类型擦除对象/Factory、捕获配置、匹配 Deleter 和任何仍依赖业务 DLL 的回调，则必须在该 DLL 驻留期间完成销毁。不能以“对象已清空”推断其控制块析构代码也独立于 DLL。

残留控制块也不能依赖业务 DLL 的 RTTI 存储；完成关闭后 TryBorrow 先通过已关闭闸门返回失效，不读取已释放对象或类型信息。

ScopeBorrow 在释放前同时保护实例和其必需代码；在途 Release 与卸载有明确同步。Pin/访问控制块残留可以在业务 DLL 卸载后释放，常驻 Runtime 必须继续存在。具体是集中非模板函数还是等价内部布局留给实现者，但须由真实 DLL 子进程回归验证。

### 6.4 释放顺序与 OS 边界

关闭新业务与装配 → 按依赖约束逐项停止/轮询 → 等待相应外部使用 → 在代码有效时销毁实例和捕获状态 → 撤销声明/Factory → 释放依赖保护 → 释放该 Module 的原生装载持有 → 使句柄失效并释放身份占位。

不能等待整个依赖图的全部内部 Borrow 归零才开始销毁；Provider 的停止与销毁须保留依赖方收尾所需能力。Busy、停止错误和 OS 释放错误均保留原始失败与进度，不强行结束；不得在 Pin 最后释放的任意业务线程上直接执行受管卸载，实际释放由串行清理协调器推进。

原生加载器可以隐式装入链接依赖，其初始化可能先于 Query 执行；这种行为不等于 Manager 已接纳/注册依赖。受管记录仍由显式候选和依赖声明确定。卸载成功表示受管生命周期完成、必要原生释放调用成功；系统/其它链接引用仍可能保持某个库驻留，不能承诺任意共享 Runtime 都被从进程移除。可卸载夹具必须排除额外宿主导入引用，并观察库级卸载事件以证明其真实路径。生产代码不得通过未登记的独立 OS Load/后台工作绕过受管寿命。

Manager 析构延续现有契约：正常调用方显式推进 Shutdown；持续工作未结束时，Shutdown 返回 Busy/具体错误并保留 Manager、资源及代码持有，可以继续 Poll，但不因超时自动销毁，也不提供 ForceUnload。析构最多尝试同步收尾一次，仍有未完成工作时进入所有配置有效的终止路径，绝不偷偷释放库。用子进程验证此误用行为。

## 7. 错误、预算与性能边界

- 可恢复失败使用既有 Result/Status；独立的操作进度与有界值报告区分前置拒绝、操作完成、清理未完成及失败后清理完成。错误域/错误码不能充当状态机。
- 保留原始 Register/Activate/Load 错误及当前清理错误，调用方原报告销毁后 Poll/Shutdown 仍可交付自有快照。报告复制/释放不得调用已卸载业务代码。
- 初始预算沿用最多 256 个受管 Module/Manager、每 Module 64 个依赖、每描述文本 256 字节、每次注册 256 声明、注册域 4096 声明、256 个 Scope、每工厂 64 个 Scope 依赖。批次计入现有预算；待清理记录占用容量，不能因无成功句柄而绕过上限。
- 元数据、Scope/Module 生命周期及控制块遵守已有无异常/OOM 终止政策，不承诺可恢复的任意分配失败；原生装载和正常校验错误必须可恢复。
- 类型校验集中在工厂装配、Dependency 解析或访问凭据形成时；访问热路径只做必要的闸门/计数保护，不重复查询 RTTI、扫描候选或解析动态符号。配置和增量构建保持 CMake/Ninja 原生图；零变化构建不得新增扫描、生成或重链接。

## 8. 迁移、规范与后续生成器衔接

设计批准后新增 ADR，明确替代范围，而非改写历史：

1. 保留 ADR-0005 的唯一 Target/主要产物原则，修订其允许逻辑 STATIC Module 嵌入 SHARED 的范围。
2. 替代 ADR-0011 第 2/3 项及 Module API 中同库 Catalog、多逻辑记录共同承载的现行契约；其它身份、依赖、注册、Scope 局部回滚和可卸载 Module 的失败收尾契约继续有效。常驻执行失败由整体 Shutdown 接管的例外按第 3.1 节明确写入新 ADR 和操作后置条件。
3. 同步 build-system、plugin-module、module-api、module-registration-lifetime、engine-host、memory-management 及索引中的混合模式措辞，区分新设计已确认与 Loader 尚未验收。
4. 历史计划、规格和验收保留当时事实，添加替代链接，不把旧 STATIC 验收改写为 Modular 成绩。
5. 将现有 `StaticModuleHost`、状态/记录命名及调用迁往统一接口，去掉仅用于旧切片的兼容别名，保留所有仍有效的测试行为。
6. 为 Foundation/Module 建立持久 ModuleId、统一 Definition/Query 和宿主常驻登记，补齐其显式启动及 Shutdown 逻辑收尾测试；保留宿主原生链接驻留。

PanguMetaTool 不在本轮实施依赖中。显式 Definition/Query、宿主 Catalog 及内部 RTTI 校验均按本文契约独立落地，不引入反射元数据系统或持久 TypeId。未来若设计生成器，应遵守 Monolithic 宿主 Catalog、Modular 单 Module Query 和强类型接口边界；生成自动化不改变本轮运行协议，也不恢复混合模式或 SHARED 内嵌 STATIC 逻辑 Module。未来持久类型身份与本轮运行期 RTTI 校验分别按各自用途设计。

## 9. 正式验收要求

测试进入 Tests，按现有 boundary/area/tier 接线；不依赖未版本化 Probe。以下覆盖是行为要求，不要求按一条一个测试机械拆分。

### 9.1 构建模式与跨 Module 链接

- 两种模式中 Foundation/Module、真实 Engine/Project Module 类型一致，错误类型配置期拒绝；ThirdParty 静态依赖及独立宿主工具正常。
- Foundation/Module 的定义和身份跨配置保持一致；常驻登记不重复 OS Load，仍按依赖顺序 Register/Activate；普通卸载请求拒绝，Shutdown 最后完成逻辑收尾且不释放宿主原生持有。
- 独立 Project 能控制 Engine 子目录模式，Game/Editor 消费集合独立；不多编未消费 Plugin，不在同图创建 Module 双变体。
- Monolithic 真实归档及最终宿主裁剪链接保留 Catalog；Modular 真实独立库、固定 Query 导出、公共 C++ 调用、Runtime 单份驻留和运行时路径部署正确。
- 模式切换往返后重编/重链接、测试发现和部署正确；无模式变化的 Ninja 无额外工作。两种模式的现有有效覆盖都不能仅因默认改成 Modular 而丢失。

### 9.2 运行中加载与失败

- 已有 Context A/World A 时接纳并激活可选 Module，随后创建 B；实例相互独立，注册只执行一次。
- B 新 Scope 装配失败只清理 B，A 可继续访问；初次 Activate 在多个已有 Scope 中途失败则回滚本 Module 的所有初次实例。
- 缺文件、缺符号、空 Query/定义/注册函数、错误协议/预期身份、重复身份、精确版本不符、依赖缺失、Optional 选择、环和预算失败均拒绝，失败不执行业务注册。
- 增量 Load 扩容、槽位复用、跨 Manager/过期句柄及错误路径不能破坏已有 Module/Scope。Load 部分失败保留清理证据，不交付部分成功结果。
- 常驻 Module 的 Register 中途失败，以及 Activate 在部分 Start 后失败且停止持续 Busy：原调用返回原始错误，Manager 自动请求整体 Shutdown，新业务接纳和启动入口关闭；普通 TryFinishUnload 继续拒绝该常驻记录，调用方可以直接 TryFinishShutdown/PollCleanup 推进。
- 常驻失败的整体关闭按依赖顺序收尾，未完成时记录、依赖及诊断句柄保持有效，原报告销毁后仍能取得原始错误与当前清理错误。重复 BeginShutdown/PollCleanup 不重复停止或销毁；PollCleanup 不单独确认常驻退出，TryFinishShutdown 成功后所有常驻句柄失效、报告仍保留原始失败，且宿主原生持有始终未释放。
- 常驻 Register/Activate 的前置拒绝不触发整体 Shutdown；已被整体关闭的 Manager 拒绝重新登记或重试启动，以新 Manager 登记时使用新的句柄 Domain。

### 9.3 Pin、Borrow 与真实卸载

- Loaded/Registered 不发业务 Pin；Active 获取成功，BeginUnload 与并发获取有确定先后；关闭后新 Pin/Borrow/装配失败。
- 外部 Pin、Borrow、延迟停止工作分别阻止相应销毁/卸载；释放后 Poll/Finish 完成且 Stop/Destroy 不重复，依赖逆序不自锁。
- 真实 DLL 中的非平凡对象、自定义 Deleter、拥有型 Factory/捕获配置必须在卸载前析构；跨 DLL RTTI 值匹配、错误类型拒绝、公共类型及兼容 STL 数据访问均验证，不能以地址相等或仅有静态链接用例替代。
- 宿主保留旧 ScopeAccess，业务 DLL 真正卸载后继续 TryBorrow、复制和最终销毁控制块安全；此时不能读取 DLL 内 type_info/type_index 或调用其释放代码。原报告销毁/Host 清理后，已交付快照仍安全。
- 重新装载同一身份取得新句柄和新实例，旧 Pin/访问凭据不能复活业务入口；不执行 Hot Reload 状态迁移。
- 单独卸载 Provider 被仍生效的依赖方阻止，先清理依赖方再清理 Provider；其余 Module/Scope 继续运行。
- OS 释放失败通过真实 Loader 外的最小私有故障注入验证保留句柄/身份/报告；正常路径必须是真实库加载和原生释放，不以 Mock 代替。
- 子进程覆盖 Manager 析构误用、DLL 初始化与库卸载观察；不把无法证明物理卸载的宿主链接库作为此项证据。

### 9.4 平台矩阵与检查

| 平台 | 必须完成 | 本轮不宣称 |
| --- | --- | --- |
| Windows | 正式默认 Modular full；Monolithic 定向受影响回归；真实加载/卸载及导出检查 | Hot Reload 或不同构建插件接纳 |
| WSL Linux/macOS | 两种模式的受影响构建/链接/CTest，真实装卸、跨 DLL/DSO 访问和失败清理 | 跨宿主 ABI 或性能等价 |
| Android | 默认 Monolithic 的真实 `.a` 链入原生宿主 `.so`；显式 Modular 的 Runtime/业务 Module 独立 `.so`、Query 导出及最小宿主实际链接 | 设备运行、设备级动态装载、APK/AAB 打包及分发可用性 |
| iOS | 默认 Monolithic 的真实 `.a` 链入最小 Mach-O 可执行文件；显式 Modular 的 Runtime/业务 Module 独立 `.dylib`、Query 导出及最小宿主实际链接 | 设备运行、设备级动态装载、签名、IPA 打包及分发可用性 |

Linux 正式源码为 `/home/moozen/WindowsGit/PanguEngine`，BuildRoot 为 `$HOME/PanguEngineBuild/Linux`；macOS/iOS 使用 `/Users/moozen/WindowsGit/PanguEngine` 与宿主本地 BuildRoot，通过 `ssh -o BatchMode=yes moozen-macos`。Windows/Android 使用仓库约定根。交叉编译不运行目标 GTest。移动端 SDK/链接不支持某配置时明确记录阻塞或经用户裁决缩小范围，不能默默回退为 STATIC 后声称 Modular 通过。

现有仅含空 main 的 Toolchain LinkSmoke 和未链接 Runtime、未提供 Module Query 的 SharedProbe 不满足上述移动端验收；本轮须增加或迁移为真正消费引擎模块的最终链接夹具。编译/符号/链接检查不能冒充目标设备上 RTTI 比较、执行或动态卸载通过。

修改自有 C++ 后使用仓库 clang-format/clang-tidy 和真实编译数据库，完成必要构建与测试；纯规格编写只检查文档内容、链接和 Diff，不为文字变更运行引擎构建。性能只报告实际测量，不提前承诺相对 STATIC 的开销。

## 10. 留给实施者的选择与批准状态

私有稳定容器、代际编码位宽、原子状态位布局、原生 Loader 私有适配布局、公共函数导出 header 的最小组织、错误码分配、固定包装模板的具体接线及测试组合，由实施计划在上述契约内决定。不要引入新的公共 Loader Manager、反射注册表或长期兼容层。

配置名 `PANGU_LINK_MODE`、Query 提案名和具体公共签名仍是实施建议。其余原先列为待确认的模式粒度、批次原子性、常驻 Module 身份与显式启动、串行管理、卸载成功含义、关闭失败政策及移动端双模式验收，已在本轮 grilling 中确认。原先的人工 TypeId/trait 方案经比较后被用户选定的强类型接口与内部 RTTI 校验替代。

用户已于 2026-11-06 批准本规格并要求提交。后续进入 writing-plans 编制实施计划，之后才修改构建和 Runtime。实现完成后分别记录设计状态、代码状态、实际平台验收及未完成项。

审阅记录：2026-11-06 初稿独立 Builder Check 结论为 `nothing important left`。随后 grilling 核实基础库缺少自身运行身份、旧移动夹具覆盖有限和原 TypeId 方案为新增范围，并由用户确认上述决策。按最终 RTTI 方案修订后，再次由独立上下文执行 Builder Check，结论为 `nothing important left`；已核对常驻 SHARED Query 与统一模式、RTTI/残留凭据、冷热路径及移动端交付边界，未发现需追加用户裁决的问题。两次检查均为规格只读审阅，不代替用户对修订稿的最终确认、代码评审或平台验收。

后续双轴 Review：Standards 0 项，Spec 发现 1 项 P2——常驻执行失败后的统一 TryFinishUnload 后置条件与普通卸载拒绝冲突。按用户修复要求，已在第 3.1、5.2、5.3、9.2 节明确整体 Shutdown 接管、推进入口、句柄失效时机和验收要求，并在第 8 节列出 ADR 同步范围。原 Spec 评审者限定复评确认该项已解决，直接相关新增问题 0 项；这是规格契约检查，不代表实现或运行验收通过。
