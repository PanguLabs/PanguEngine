# UE / O3DE 动态模块代码生命周期核对

日期：2026-11-17。研究问题：当 Entity、Component、World 或反射数据中仍存在指向 Gameplay/Gem 模块的代码入口时，成熟引擎如何约束模块卸载；正常退出时，Gameplay 清理与模块关闭如何排序。

本次只核对参考项目源码，不修改 Pangu ECS 草案或实现。下文明确区分源码事实与推断；没有将局部搜索结果当成整个引擎不存在某种机制的证明。

## 版本与核对范围

- **O3DE：**本地 `D:/Git/o3de`，`git rev-parse HEAD` 为 `3db6943249d8bd7960b9ed7e9aee310b7668586e`；提交日期 `2026-05-27 09:35:03 -0700`，标题为 26.05 release stabilization 合并。`engine.json` 的有效 `version` 为 `2.6.0`，同文件还保留 `O3DEVersion=0.1.0.0`、`display_version=00.00` 等字段，因此不使用后两者推断发布版本。[本地版本字段](D:/Git/o3de/engine.json:2)
- **UE：**由主代理核对本地 `D:/Git/UnrealEngine` 源码，版本 `5.8.3`，HEAD `396c9f059903aed5fec78ecd3d437a40c6415368`（`refs/heads/5.8.3`）。[Build.version](D:/Git/UnrealEngine/Engine/Build/Build.version:2)
- 结论以本地实际文件为准；未对两个参考仓库进行构建，也不声明工作区与 HEAD 完全一致。O3DE 没有 `.codegraph/`，按约定用 `rg` 定位后读取相关方法；UE 的 CodeGraph 调用长时间无响应并终止，主代理随后直接读取具体源码。核对范围集中于 ModuleManager、ComponentApplication、Entity、descriptor/反射清理及 UE 退出链路，未审查全仓所有插件的自定义热重载实现。

## O3DE：默认 Gem 驻留，加有序清理；显式模块引用可延长生命周期

### 源码事实：存在模块级引用保持，不能概括为“没有 Pin”

`ModuleManagerRequests::LoadModuleOutcome` 返回 `AZStd::shared_ptr<ModuleData>`。接口文档明确同一个模块的多次加载共享 `ModuleData`；`maintainReference=true` 时，ModuleManager 捕获自己的引用，使模块保留到应用关闭。实际实现用 `shared_ptr<ModuleDataImpl>` 和按名称保存的 `weak_ptr`，并在 `maintainReference` 为真时加入 `m_ownedModules`。[公共契约](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/ModuleManagerBus.h:93)；[实际持有引用](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/ModuleManager.cpp:346)；[成员所有权](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/ModuleManager.h:169)

正常 `ComponentApplication::LoadModules` 分三轮执行 Load、CreateClass、RegisterComponentDescriptors，调用 `LoadDynamicModules(..., true)`。因此应用正常加载的动态 Gem 由应用级所有者保持驻留，不会仅因某个局部加载结果离开作用域而卸载。[正常加载路径](D:/Git/o3de/Code/Framework/AzCore/AzCore/Component/ComponentApplication.cpp:1597)

**推断：**这是一种模块级 lifetime handle，调用者保存返回的 `shared_ptr` 可以延迟 `ModuleDataImpl` 析构；正常 Gem 的应用级驻留也是代码生命周期保障。它与“每个 Entity/反射回调自动持有一个 CodePin”不是同一种粒度，不能因为没有后一种包装就说 O3DE 没有引用保持机制。

### 源码事实：正常关闭时，游戏实体和反射先清理，再卸载 Gem

`ComponentApplication::Destroy()` 的实际顺序如下：[Destroy 完整链路](D:/Git/o3de/Code/Framework/AzCore/AzCore/Component/ComponentApplication.cpp:988)

1. 执行已排队的 Tick/SystemTick 回调并禁止继续排队，随后 Finalize 用户设置（990–999）。
2. 遍历 `m_entities`，删除除根 SystemEntity 以外的实体（1001–1015）。实体析构会先在 Active 状态调用 `Deactivate()`，然后逆序删除组件。[Entity::Reset](D:/Git/o3de/Code/Framework/AzCore/AzCore/Component/Entity.cpp:96)
3. 游戏实体销毁后、模块卸载前强制脚本 GC。源码注释直接说明目的是清理指向反射类/EBus 的引用，并指出反射数据引用计数是另一种可能解法（1017–1020）。
4. 先 `m_moduleManager->DeactivateEntities()`，再 deactivate 根 SystemEntity。注释明确在这一刻根 SystemEntity 的组件仍允许引用模块数据，所以不能提前卸载模块（1022–1033）。
5. `DestroyReflectionManager()`，随后清 SettingsRegistry 的 notifier/merge event；在卸载前清理全局 SerializeContext 泛型 ClassInfo（1038–1052）。注释明确 ClassInfo 可能持有来自模块的符号，必须先清。
6. `m_moduleManager->UnloadModules()`（1055）；然后才 `m_systemEntity.reset()`（1058），最终广播 `ReleaseDescriptor` 释放应用剩余 descriptor（1062–1063）。

这里必须保留一个细节：**根 SystemEntity 在 DLL 卸载前停用，但对象本身在卸载后才 reset**，所以“所有 Entity 对象都在模块卸载前析构”并不符合这段源码。常规模块系统组件由 `ModuleDataImpl::m_moduleEntity` 持有；`ActivateEntities()` 创建 ModuleEntity、实例化模块要求的组件，并把这个实体从 ComponentApplication 的通用实体集合移除，转由模块侧管理。[ModuleEntity 归属](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/ModuleManager.cpp:667)；[从应用集合移除](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/ModuleManager.cpp:709)

### 源码事实：ModuleEntity → Module/descriptor → DLL

`ModuleManager::DeactivateEntities()` 先将 owned ModuleEntity 标成 Deactivating，按 `m_systemComponents` 的逆序逐个停用组件，再将实体改成 Init 并清组件指针列表。`UnloadModules()` 调用此步骤后，从 `m_ownedModules` 尾部逐个 `pop_back()`，即反初始化顺序释放管理器引用；实际析构仍由共享引用是否归零决定。[停用组件](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/ModuleManager.cpp:217)；[释放模块引用](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/ModuleManager.cpp:263)

`ModuleDataImpl::~ModuleDataImpl()` 明确执行以下顺序：[模块最终析构](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/ModuleManager.cpp:134)

1. `m_moduleEntity.reset()`，在 DLL 尚在时删除模块系统组件。
2. 动态模块通过 DLL 导出的 `DestroyModuleClass` 销毁 `AZ::Module`；静态模块直接 `delete`。
3. 最后 `m_dynamicHandle->Unload()`，再销毁 handle。

descriptor 并不是 `ModuleDataImpl` 的独立成员。`AZ::Module` 持有 `m_descriptors`；其析构逐个调用 `ReleaseDescriptor()`。因此准确的顺序是 ModuleEntity/组件 → 派生 Module 析构及基类 Module 的 descriptor 清理 → 动态库卸载。[descriptor 成员与契约](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/Module.h:51)；[Module 析构](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/Module.cpp:17)

本次核对的 `ModuleManager` 没有独立名为 `Shutdown()` 的入口；应用显式调用 `UnloadModules()`，管理器析构也会调用它。[ModuleManager 析构](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/ModuleManager.cpp:192)

### 源码事实：反射回调通过主动 unreflect 清理

注册 descriptor 时，ComponentApplication 将捕获 descriptor 指针的 lambda 交给 ReflectionManager；注销时按 descriptor UUID 调用 `Unreflect`。`ComponentDescriptor::ReleaseDescriptor()` 先要求应用注销，再 `delete this`。[注册/注销](D:/Git/o3de/Code/Framework/AzCore/AzCore/Component/ComponentApplication.cpp:1234)；[ReleaseDescriptor](D:/Git/o3de/Code/Framework/AzCore/AzCore/Component/Component.cpp:196)

`ReflectionManager::Unreflect(TypeId)` 对各 ReflectContext 逆序开启 remove 模式，调用原反射入口执行反注册，再删除保存的入口。正常退出的 `DestroyReflectionManager()` 会先 `Clear()`；`Clear()` 逐个移除 context，移除时在 remove 模式下逆序调用所有入口，然后清入口集合。因此反注册及回调包装本身的析构发生在模块 DLL 仍加载时。[单入口 unreflect](D:/Git/o3de/Code/Framework/AzCore/AzCore/RTTI/ReflectionManager.cpp:61)；[Clear](D:/Git/o3de/Code/Framework/AzCore/AzCore/RTTI/ReflectionManager.cpp:24)；[移除 context](D:/Git/o3de/Code/Framework/AzCore/AzCore/RTTI/ReflectionManager.cpp:187)；[销毁 ReflectionManager](D:/Git/o3de/Code/Framework/AzCore/AzCore/Component/ComponentApplication.cpp:1444)

`AZ::Module::Reflect` 的注释专门禁止覆盖此入口，要求把非组件类型的反射放进组件反射入口，原因就是确保所有 ReflectContext 都能取消反射。全局泛型 ClassInfo 的 `Cleanup()` 也会从登记的 SerializeContext 移除 GenericClassInfo，并销毁相应对象。[Module::Reflect 约束](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/Module.h:61)；[泛型反射数据清理](D:/Git/o3de/Code/Framework/AzCore/AzCore/Serialization/SerializeContext.cpp:2314)

### 运行中卸载的边界

- **已确认的能力：**模块数据使用共享所有权；底层确实提供 `DynamicModuleHandle::Unload()`，Windows 最终调用 `FreeLibrary`。因此不能笼统说 O3DE 不支持运行中卸载。[Unload](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/DynamicModuleHandle.cpp:77)；[Windows 卸载](D:/Git/o3de/Code/Framework/AzCore/Platform/Common/WinAPI/AzCore/Module/DynamicModuleHandle_WinAPI.cpp:145)
- **已确认的默认策略：**应用正常 Gem 加载保持管理器引用直到关闭。`LoadDynamicModule(..., maintainReference=false)` 的返回引用可由调用者管理，但这不是自动迁移、销毁所有外部 Entity 的协议。[接口契约](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/ModuleManagerBus.h:95)
- **局部源码事实：**本次读取的通用 Entity 组件集合是 `vector<Component*>`；默认 descriptor 创建组件只执行 `aznew ComponentClass`；ReflectionManager 的入口保存函数/TypeId，没有 ModuleData 引用字段。上述常规创建与反射注册路径没有展示自动附着模块 `shared_ptr` 的步骤。[Entity 组件集合](D:/Git/o3de/Code/Framework/AzCore/AzCore/Component/Entity.h:46)；[默认创建](D:/Git/o3de/Code/Framework/AzCore/AzCore/Component/Component.h:686)；[反射入口结构](D:/Git/o3de/Code/Framework/AzCore/AzCore/RTTI/ReflectionManager.h:63)
- **限制与推断：**`ModuleDataImpl` 最终析构直接执行上述清理和卸载，没有在该析构路径中查询所有外部游戏实体、任意 EBus handler、线程或用户回调是否还引用模块代码。结合正常退出中显式删除实体、GC、unreflect 的顺序，可推断调用者必须先完成相应使用者清理，或保持模块引用；不能把“释放最后一个 ModuleData 引用”理解成自动证明任意外部引用都安全。组件宏的文档也明确警告 descriptor 释放后不能继续使用组件实例。[最终析构](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/ModuleManager.cpp:134)；[组件使用约束](D:/Git/o3de/Code/Framework/AzCore/AzCore/Component/Component.h:365)
- **引用边界：**不能据此承诺 ModuleData 可无条件活得比 ModuleManager 更久：动态模块共享指针的 deleter 捕获管理器 `this`；管理器析构会诊断仍打开的 non-owned handle。这里核对出的用途是管理器有效期间的模块引用，不是独立于管理器的全局永久 pin。[deleter](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/ModuleManager.cpp:354)；[关闭诊断](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/ModuleManager.cpp:200)

另一个容易混淆的引用计数是 `DynamicModuleHandle` 的 `m_initialized` EnvironmentVariable：它记录模块初始化 token 的引用与 owner，以确定谁调用初始化/反初始化；不能等同为所有 Component/回调的 code pin。Environment 对带虚函数对象的文档还明确指出模块卸载时需销毁依赖其 vtable 的变量，或由使用者控制生命周期。[初始化 token](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/DynamicModuleHandle.cpp:46)；[token 字段](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/DynamicModuleHandle.h:124)；[Environment vtable 约束](D:/Git/o3de/Code/Framework/AzCore/AzCore/Module/Environment.h:47)

## UE：正常退出保留 DLL；动态卸载有多条路径

以下 UE 源码事实由主代理在本地 UE 5.8.3 文件中核对。

### 源码事实：正常关闭 Module 对象，但不立即卸载 DLL

`FModuleManager::UnloadModulesAtShutdown()` 对支持自动关闭的模块按 LoadOrder 逆序处理，先对全部目标调用 `PreUnloadCallback()`，再逐个 `UnloadModule(..., bIsShutdown=true)`。`UnloadModule()` 会调用 `ShutdownModule()` 并 reset Module 接口对象；其 shutdown 分支明确保留 DLL 到 OS 进程退出，避免其他模块随后执行析构/虚函数时进入已卸载代码。[关闭排序](D:/Git/UnrealEngine/Engine/Source/Runtime/Core/Private/Modules/ModuleManager.cpp:1438)；[UnloadModule 与 shutdown 分支](D:/Git/UnrealEngine/Engine/Source/Runtime/Core/Private/Modules/ModuleManager.cpp:1317)

`FEngineLoop::Exit()` 先 `GEngine->PreExit()`、后 `AppPreExit()`，随后才统一进行模块 shutdown；调用旁的注释再次说明不会实际卸载 DLL。[引擎 PreExit](D:/Git/UnrealEngine/Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:5065)；[AppPreExit](D:/Git/UnrealEngine/Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:5123)；[模块关闭](D:/Git/UnrealEngine/Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:5177)

`UGameEngine::PreExit()` 遍历 WorldList，执行 `BeginTearingDown()`、Actor 的 `EndPlay(Quit)`、GameInstance 的 `Shutdown()`、`CleanupWorld()`。这证明 World/Gameplay 清理先于上述 Module shutdown，但 `CleanupWorld()` 不等于所有 UObject 均已物理析构或 GC 结束。[World 退出链路](D:/Git/UnrealEngine/Engine/Source/Runtime/Engine/Private/GameEngine.cpp:1341)

`IModuleInterface` 还规定 `StartupModule()` 中加载的依赖在 `ShutdownModule()` 中可用；`SupportsDynamicReloading()` 默认返回 true，是模块能力声明。[依赖生命周期契约](D:/Git/UnrealEngine/Engine/Source/Runtime/Core/Public/Modules/ModuleInterface.h:45)；[ShutdownModule 契约](D:/Git/UnrealEngine/Engine/Source/Runtime/Core/Public/Modules/ModuleInterface.h:72)；[动态重载能力](D:/Git/UnrealEngine/Engine/Source/Runtime/Core/Public/Modules/ModuleInterface.h:88)

### 源码事实：运行中卸载不能与 shutdown 驻留策略混同

`UnloadOrAbandonModuleWithCallback()` 会依据实际分支选择 unload 或 abandon；`AbandonModule()` 仍执行 Shutdown/Reset，但不调用 `InternalFreeLibrary()`。这不代表所有卸载入口都强制检查 `SupportsDynamicReloading()`；本次不作这种概括。[Unload/Abandon 选择](D:/Git/UnrealEngine/Engine/Source/Runtime/Core/Private/Modules/ModuleManager.cpp:1989)；[Abandon](D:/Git/UnrealEngine/Engine/Source/Runtime/Core/Private/Modules/ModuleManager.cpp:1409)

普通非 `UE_MERGED_MODULES` 分支的 `InternalFreeLibrary()` 直接 `FreeDllHandle()`，所以正常 shutdown 的 DLL 驻留规则不能推广到所有运行中卸载。[非 merged 卸载](D:/Git/UnrealEngine/Engine/Source/Runtime/Core/Private/Modules/ModuleManager.cpp:733)

UE 5.8.3 另有仅在 `UE_MERGED_MODULES` 编译分支下生效的动态库模块引用与延迟卸载逻辑：通知 UObject 清类，等待 GC，再经 `CheckLiveObjectsInModulesCallback` 确认没有存活对象后才 `FreeDllHandle()`；还可检测陈旧 delegate。它是具体构建分支中的生命周期保障，不能表述为所有 UE 构建都有的通用 CodePin。[merged 引用/排队逻辑](D:/Git/UnrealEngine/Engine/Source/Runtime/Core/Private/Modules/ModuleManager.cpp:603)；[GC 与存活对象检查](D:/Git/UnrealEngine/Engine/Source/Runtime/Core/Private/Modules/ModuleManager.cpp:885)

官方公开 API 可作为类型及入口的辅助参考，但上述顺序和条件分支以本地源码为依据：[FModuleManager 官方 API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FModuleManager)；[IModuleInterface 官方 API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/IModuleInterface)。

## 与 Pangu 问题有关的比较结论

| 问题 | O3DE 本次核对结果 | UE 本次核对结果 |
| --- | --- | --- |
| 正常加载后如何维持代码有效 | 应用正常加载 Gem 时持有共享 ModuleData 引用直到关闭 | 正常 shutdown 保留 DLL 到 OS 进程退出 |
| Gameplay 清理是否先于 Module 关闭/卸载 | 普通游戏 Entity、脚本 GC、模块组件停用、反射清理先于 DLL 卸载；根 SystemEntity 对象最后 reset | World/Gameplay PreExit 先于 Module shutdown；并不据此声称全部 UObject 已销毁 |
| 模块自有对象与代码的顺序 | ModuleEntity/组件 → Module/descriptor → DLL | Module shutdown/reset；正常退出 DLL 留驻 |
| 可否据此声称存在统一的逐回调 CodePin | 本次常规 Entity/反射路径未展示自动逐回调持有 ModuleData；但存在显式模块引用保持 | 有驻留、Abandon 与特定 merged 构建的延迟卸载；不能泛化为每种构建都有统一 Pin |

**推断（供 Pangu 后续设计判断，非本次实施方案）：**两者共同证明了“代码的使用者尚未清理时，不能贸然卸载该代码”的生命周期要求；实现手段包括更长生命周期的模块所有者、退出时保留动态库、显式使用者清理和受条件限制的延迟卸载。由这些源码不能推出“成熟引擎没有 Pin”，也不能推出“每个 World 额外持有独立 CodePin 是唯一方案”。Pangu 的 World 若保留 Gameplay 的 hooks、析构函数、system/observer 回调或反射入口，需要在其自己的所有权和卸载契约中证明这些入口的有效期；本次研究不选择或修改该契约。
