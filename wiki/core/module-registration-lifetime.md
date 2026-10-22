# Pangu Module Registration & Lifetime v1

> 设计状态：Frozen（注册、依赖、生命周期与 CodePin 的设计语义）；Public API、二进制入口布局及同步实现未冻结
> 更新：2026-10-22
> 决策：[ADR-0008：统一 Module 注册与生命周期](../decisions/0008-module-registration-lifetime.md)、[ADR-0011：Module API 行为契约](../decisions/0011-module-api-behavior-contract.md)
> 权威边界：[Plugin / Module](plugin-module.md)、[Service](service.md)、[Memory](memory-management.md)、[EngineHost](engine-host.md)
> 构建/错误前提：[Build System](../foundation/build-system.md)、[ABI](../foundation/abi.md)、[Error / Diagnostics](../foundation/error-diagnostics.md)

## 1. 目标及非目标

本规范为一个 Pangu Module 定义静态/动态统一的**语义注册契约**，以及与注册、实例创建、异步使用、停用、卸载关联的生命周期。这里的语义边界已冻结；具体类型签名与二进制结构仍待设计。其目的不是建立新的 Module 类型，也不是实现万能 Service Manager。

既有 Frozen 前提：

- 一个 Module = 一个实际 CMake `STATIC`/`SHARED` Library Target、一个主要库产物；Plugin 可以包含多个 Module，也可以只包含 Assets。
- Concrete / Contract / Registry Service 三模式；Host/Context/World 是实例 Owner；Module 是代码 Owner。
- 不新增 `BinaryImage` 架构层、`ExtensionDescriptor`、`ExtensionManager`、`ISubsystem`、统一 `IService`。
- CMake 是构建关系的权威来源；`Module.toml` 非必选；不强制整个引擎采用 C Bootstrap。
- C++23、禁用异常；可恢复失败遵循 `Result/Error`，重要操作使用独立有界失败报告。
- ABI 相容不等于所有权与代码驻留安全；CodePin 不能代替 ServiceRef/Borrow 的实例保护。

非目标：Plugin 分发/安装清单、完整 Hot Reload/Hot Swap、Provider 选择算法、具体 Error Domain 编码、所有 C++ Public API/ABI 布局、移动端运行时动态装载政策。

## 2. 身份与数据所有权

- `ModuleId` 是稳定的**逻辑代码单元身份**，不得依赖内存地址、加载顺序、`std::hash` 或运行时递增序号；与 `PluginId`、Service Contract Identity、Error Domain ID 分离。
- v1 每个受管装载域中，一个逻辑 ModuleId 同时至多有一个生效版本；相同身份的重复装载及不同版本并存均必须拒绝。身份占位从装载预留持续到回滚/卸载清理完成；Quiescing、Busy 或失败待清理的记录仍占用该身份，不能因此装入第二份。多代际并存仅属于未来 Hot Reload 专题。多个 EngineHost 并行运行也不是 v1 已承诺的能力。
- `ModuleDescriptor` 在语义上描述逻辑身份、协议版本、构建身份/ABI 相容要求与 Module 依赖；已解析的代码入口可以通过独立的运行期句柄持有。运行期状态、借用计数、动态库句柄、错误现场不放入持久 Descriptor；Descriptor 的确切字段/布局均未冻结。
- Module 的描述信息可能起初指向其自身代码/只读数据；Module 管理器需在持有代码驻留保护时复制所有要跨卸载保留的身份、依赖、诊断字符串。不能将插件字符串视图长期挂在 Host 上。
- Module 构建依赖（CMake Target/Link 边）与**运行期 Module Dependency** 不完全等价；不得从 `target_link_libraries` 自动推导全部生命周期依赖。静态强类型调用应由构建依赖和实际代码驻留保证共同覆盖。
- `ModuleId` 的 128 位身份/规范文本及精确版本、显式依赖绑定行为由 [Module API v1 第 2 节](module-api.md#2-身份与描述数据) 冻结，C++ 布局和生成工具仍待设计；ABI Profile 字段及 Fingerprint 算法已由 [ABI Profile / Fingerprint v1](../foundation/abi-profile-fingerprint.md) 冻结，本文件只引用其契约。

## 3. 静态与动态模块的发现/注册

### STATIC Module

1. 构建/生成阶段显式产生静态 Module Catalog；宿主启动时将确定的 Catalog 项交给 Module 管理器。
2. Catalog 直接引用每个 Module 的注册入口，强制链接器保留所需代码；不依赖跨 Translation Unit 的静态初始化顺序、全局构造副作用或对所有静态库无条件 `whole-archive`。
3. Catalog 不需要新的 Module Library Target；PanguMeta 生成代码属于对应 Module 的唯一 Library Target。未引入 PanguMeta 前允许由 CMake/明确编写的桥接入口生成 Catalog 验证探针。
4. STATIC 库归档本身不可卸载；若静态代码被链接进可卸载动态库，其代码驻留跟随**实际承载它的动态库**，不是归档文件。

### SHARED Module

1. Module 加载器在通过装载前可信产物绑定与 C++ ABI/Public Module API Contract 预检后，才允许获取动态库句柄；通过一个版本化稳定 `extern "C"` 导出名定位**原生 C++23 ModuleDefinition Query**（候选 `PanguModuleQueryV1`），该 C Language Linkage 仅用于**按名找符号**，不是 C POD 交换协议或全引擎 C Bootstrap。
2. STATIC 和 SHARED 使用同一种原生 C++23 `ModuleDefinition`、Descriptor 和 `RegistrationContext`，SHARED **不再要求窄 C POD Discovery、数值 C 状态码转换或 `void*` 注册桥**。仅在真实加载产物的 ABI Profile/Fingerprint、协议版本及 ModuleDefinition Public API Contract 均完成**装载前**预检时，才可按受控 C++ ABI 调用 Query；装载后二次复核通过后执行统一的 C++ 注册事务。
3. 需要区分构建/安装阶段的预检与装载后的嵌入数据验证：`LoadLibrary`/`dlopen` 可能在取得导出入口之前运行 DLL 初始化器/全局构造，因此运行时符号校验**不能**宣称所有插件代码已在执行前受到 ABI 验证。装载前预检及装载后二次验证遵循 [ABI Profile v1](../foundation/abi-profile-fingerprint.md)；禁止依赖全局构造产生业务注册副作用。预检不等于防恶意代码的沙箱或签名机制。
4. 动态导出入口由 CMake/PanguMeta 等生成，不要求业务 Module 手写导出符号或注册桥；ABI 指纹算法和字节表示遵循 ABI Profile v1。查询函数返回的 C++ Descriptor/依赖字符串等数据在 CodePin 有效期内借用并由 Host 复制；精确符号名称、C++ Public API 布局/契约和平台调用约定仍待验证。此技术路线依 [ADR-0010](../decisions/0010-unified-cpp-module-entry.md) 修订。
5. 加载后的动态库句柄与 CodePin 由 Module 机制拥有和协调；不为文件句柄再建独立 BinaryImage/管理层。

两种装载来源通过不同的物理定位方式取得**同一种 C++23 ModuleDefinition**，之后使用完全一致的 C++23 注册、依赖、事务与生命周期协议；SHARED 的固定符号名不构成独立的 C 数据交换层。

## 4. 注册与实例化分离

1. Module 注册只负责声明/安装其可用能力、工厂、Contract Provider 和业务 Registry Contribution；**成功注册不等于某个 Service 实例 Ready**。
2. Host 级静态 Concrete Service 不必注册进动态 Registry。已有业务 Module 可以持有 Registry Service；Contribution 是可撤销的条目而非自动创建的 Service。
3. 多个 EngineContext/WorldContext 要在各自实例归属 Scope 内安装适用的 Provider/Contribution，不能把进程级注册直接当作每个 Scope 的运行时实例。
4. Module 注册过程使用**显式事务**（暂存 → 校验冲突/依赖 → 发布）；在每个目标 Scope/Registry 的同一事务内，对业务观察者必须表现为全部成功或全部失败，任何失败均撤销未发布资源及注册，不得留下半可见 Provider。跨多个 Scope 不承诺隐式的进程级原子提交：由 Host 在可见性闸门下完成相关 Scope 的一致性验证和激活，不能让业务在部分 Scope Ready 时误用未完成的装配。
5. 注册句柄或 Token 必须记录原有 Owner、撤销能力和必要的 CodePin 保护。撤销先关闭新获取入口，完成在途调用/实例清理后才释放代码；不能仅从 Registry 容器删除指针就视为卸载完成。
6. 注册撤销和 CodePin/ServiceRef 的争用必须由同一可验证的同步规则裁决，不允许在 Registry 锁内执行未知 Provider 回调/析构。

## 5. Module 与 Service 的两套依赖图

- Module Dependency：代码可用性、装载/注册/卸载顺序以及运行期 Module 驻留需求。
- Service Dependency：Scope Owner 的 Service 实例、动态 Provider 选择、Ready 与 Borrow/Lease 生命周期。
- 两者不是一张图；Host 协调器在绑定具体 Provider 后必须检查**有效生命周期约束的合并结果**没有互相矛盾的顺序或环。
- 必需依赖缺失、重复 ModuleId、无效 Scope 方向、单 Provider Contract 冲突，在发布/激活前失败，并走注册事务/已创建实例的逆序回滚。
- 可选依赖只能以显式发现及检查结果使用；一旦绑定并在调用或清理期间需要 Provider 存活，仍须建立相应 Guard/Lease 并纳入有效依赖图校验，不能以“可选”为由遗漏生命周期约束。Provider 选择不使用“最后注册者胜出”。

## 6. 建议的 Module 状态与转换

下述**阶段语义已冻结**；状态名称和阶段映射仅为示意，不冻结枚举、类继承或每一步对应一个虚函数。业务可见性还必须满足相应 Service 的 Ready 条件：

```text
Discovered -> Verified -> Loaded -> Registered -> Active
                                      |           |
                                      +-----+-----+
                                            v
                                        Quiescing
                                            v
                                         Draining
                                            v
                                         Stopped
                                            v
                                       Unregistered
                                            v
                                         Unloaded
```

- Discovered/Verified：发现候选，检查身份、协议及可预检的 ABI/依赖；不能仅因为发现就调用用户代码。
- Loaded：STATIC Catalog 已定位，或 SHARED 句柄/入口已获得；尚未发布 Module 能力。
- Registered：注册事务提交完成；工厂/能力供协调器继续装配，不承诺 Scope 内 Service Ready，也不向外部业务开放调用。业务入口必须满足 Module Active，以及所依赖 Service 的 Ready 条件（如有）；不为独立 Contribution 强造 Service 实例。
- Active：Module 允许新的业务能力使用；Scope 内具体 Service 仍需自身 Ready 校验。不是所有 Module 都要求启动后台工作或统一 Tick。
- Quiescing：禁止新的业务进入和新的借用，已合法取得的 Guard/Lease 可以继续完成清理。
- Draining：协调 Task、Callback、Service Borrow、Registry Contribution、Resource/GPU 与 Allocator 依赖；超时/Busy 不等于停止成功。
- Stopped/Unregistered：依赖逆序执行清理，在代码和所依赖实例仍有效时析构，之后撤销注册并释放必要 Guard；实际撤销可分为“禁止新访问”和“最终销毁”两步。
- Unloaded：逻辑 Module 已完成清理并释放自己的代码承载持有。依据 [ADR-0011](../decisions/0011-module-api-behavior-contract.md)，同一 SHARED 承载的其它 Module/CodePin 仍需要代码时，Manager 继续保留实际 OS 句柄；全部持有结束后才实际卸载。最后持有的 OS 卸载未完成仍属清理未完成；STATIC 归档不执行 OS 卸载。详细接纳和退出规则以 [Module API v1 第 3 节](module-api.md#3-static-catalog) 为准。

部分状态可以作为内部子阶段，不需要 Public Enum 暴露。注册阶段失败按已完成步骤逆序回滚；Active 阶段启动失败按 Service DAG/Module 依赖图逆序收尾；失败报告保留首个主错误、清理错误作为次要记录。进入 Quiescing 后若外部 Borrow 未释放，保持拒绝新业务的状态并返回 Busy，**不得假装卸载成功或在仍有借用时重新开放业务入口**。

Draining 与依赖逆序析构必须协同推进，遵循 [Service 的生命周期与依赖](service.md#生命周期与依赖)：先排空某个依赖方的外部使用者，再销毁它并释放其持有的依赖 Borrow，之后处理被依赖方。**不得把整个 Module 或依赖闭包的 Borrow/Pin 全部归零作为开始任何析构的前提**。Registered 阶段退出也必须收尾装配期间已经启动的任务和已创建实例；若确无工作和借用，相应清理阶段可以为空操作。

## 7. CodePin 和实例驻留

- `ModuleCodePin` 由 Module 管理器统一发放，保护回调、析构器、Factory、deleter 等**代码**的驻留；它不保护独立的 Service 实例内存，后者使用对应 Scope Owner/ServiceRef/Borrow 的生命期机制。
- 模块执行用户回调、创建对象、登记异步任务之前要先获取有效代码/实例保护；异步任务获取保护发生在**入队之前**，不能等 Worker 开始才补救。
- Quiescing 与新业务 Guard 获取必须有可验证的线性化顺序：进入 Quiescing 后拒绝新的**外部业务**访问，允许先前合法持有者完成，并通过仅供协调器使用的内部清理权限保留 Destroy、撤销与依赖访问所需的 CodePin/Lease。不得将这些内部权限再次开放给新的业务请求。
- Module 管理器的装载所有权与外部 CodePin/实例借用要区分；不能把加载器的基本持有误当成永远无法归零的外部 Pin，也不得等待本次卸载自身必要的清理 Pin 提前归零。
- Registry Contribution 的注销要先拒绝新调用、等待所有在途执行结束，再在代码驻留期释放其状态；Service 清理完成后释放持有的依赖 Borrow。
- 代码在 STATIC Module 中时，Pin 的释放不能错误地对 `.a` / `.lib` 执行卸载；若该代码物理上位于可卸载 SHARED 容器中，必须保护那个实际承载库的驻留。此为 ModuleLoader 内部账本，不新增 BinaryImage 层。
- 一旦借用、任务、资源或 Callback 有未解除依赖，应保留必要 Module 记录和动态库句柄，向调用者返回未完成/Busy；绝不强制 `dlclose`/`FreeLibrary`。

## 8. C++23 API 方向（非冻结签名）

[ModuleDescriptor & Registration API v1](module-api.md) 当前为 **Partially Frozen**：依 [ADR-0011](../decisions/0011-module-api-behavior-contract.md) 冻结身份/版本、依赖阶段、Catalog 可达性、Scope 局部回滚与操作后置条件；本文件继续定义基本生命周期边界，具体调用行为以该文档为准。STATIC/SHARED 的统一原生 C++23 路线保持 [ADR-0010](../decisions/0010-unified-cpp-module-entry.md) 约束；精确 ModuleDefinition 布局、Query 符号名、Progress/Report 和方法签名仍未冻结。

```cpp
namespace pg::module
{
struct ModuleId;
struct ModuleDescriptor;
struct ModuleLoadOptions;
struct ModuleHandle;
class ModuleCodePin;
class RegistrationContext;
class ModuleRuntimeContext;
class ModuleManager;

// 仅示意可恢复失败与显式操作级报告的集成方式：
// [[nodiscard]] Result<ModuleHandle> Load(ModuleId, ReportContext&) noexcept;
// [[nodiscard]] Status Activate(ModuleHandle, ReportContext&) noexcept;
// [[nodiscard]] Status BeginUnload(ModuleHandle, ReportContext&) noexcept;
// [[nodiscard]] Status TryFinishUnload(ModuleHandle, ReportContext&) noexcept;
} // namespace pg::module
```

- Module 入口可以是自由函数/生成函数，不要求实例化统一 `IModule` 或 `IService` 虚基类；Module 发现/注册入口使用动态函数指针，不改变 Contract Service、Registry Contribution 等已有的动态分发契约。
- `RegistrationContext` 提供带 Owner 的撤销注册与事务能力；Runtime Context 提供已解析的显式依赖、正确 Scope 与停止协调，不通过隐式全局单例获取“当前 Host”。
- 需要启动/停止工作的 Module 可提供显式钩子，但不能将它推广为每个 Concrete Service 必须实现同一生命周期接口。
- 可信装载前 ABI/Public API Contract 预检与实际加载产物的绑定、身份的 C++ 对象布局、原生 Status/Error 编码、SHARED 导出符号/C++ 函数签名、ModuleHandle/CodePin 和 Progress/Report 的 API 仍未冻结，不以此示意代码代替工具链验证。

## 9. 最小验证矩阵

| 场景 | 验收目标 |
| --- | --- |
| STATIC Catalog，链接器裁剪 | 有明确根引用的注册代码保留、无依赖全局构造顺序 |
| SHARED 模块加载 | 稳定导出名解析出原生 C++23 Query；不兼容或预检产物绑定失败**不得 OS Load/C++ Query**；装载后二次数据失配不得注册 |
| 加载器构造副作用 | 验证平台入口/全局构造的实际执行时序，不宣称 Load 后 ABI 检查能阻止 DllMain |
| 单 Module 单库 | 生成代码归入唯一 CMake STATIC/SHARED Target，无第二个 Module 库 |
| ModuleId 重复、依赖缺失、图中环 | 拒绝发布，明确错误且可重试/回滚 |
| 注册事务第 N 项失败 | 前 N-1 项无半发布，资源与 Token 全部一致撤销 |
| Provider 与 Scope | 多 Context/World 独立注册；Contract 单 Provider 限制，业务 Registry 多 Contribution |
| 持有 ServiceRef/Callback/Task | Quiesce 关闭新入口，已有工作能收尾；卸载返回 Busy 并保留代码 |
| 同 Module/跨 Module 的 Service 依赖链 | 逐个排空和逆序析构，内部依赖 Borrow 不导致停止永久 Busy |
| Registered 装配中失败 | 已启动任务和实例完成收尾，未 Active 的 Module 不被业务访问 |
| Busy/回滚待清理期间重复装载 | 原 ModuleId 保持占用，清理完成前拒绝同身份的新装载 |
| 在途析构和跨 Module Allocator | 先清理借用与对象，再释放 CodePin 和动态库句柄 |
| STATIC 被链接进动态承载库 | Pin 正确保护实际承载代码的动态库 |
| 失败/回滚与报告 | 原始 Error 保留，操作级有界报告可在模块卸载后阅读 |
| Windows、WSL Linux、macOS | 独立验证代码驻留与原生链接行为；Android/iOS 首先做平台能力和链接验证，不将其等同于设备运行验收 |

## 10. 冻结范围与后续设计

**已冻结的语义**：身份独立性与单生效版本、显式 Static Catalog、STATIC/SHARED 统一 C++23 ModuleDefinition/Registration（SHARED 通过版本化稳定导出名定位；无额外 C POD）、可信装载前 ABI/API 预检与装载后二次复核、注册事务/Scope 可见性、双依赖图、生命周期阶段和失败回滚、ModuleCodePin 与 ServiceRef 分工、清理内部权限、不可强制卸载。依据 [ADR-0008](../decisions/0008-module-registration-lifetime.md) 与修订它的 [ADR-0010](../decisions/0010-unified-cpp-module-entry.md)，不改变 ADR-0001～0007。

**未冻结**：具体 ModuleId/Descriptor C++ 布局、SHARED Query 的稳定符号名/平台调用约定、原生 C++ ModuleDefinition Public API/接口契约、预检产物绑定技术、PanguMeta 生成协议、CMake 包装 API、生命周期 Enum/公开函数签名、同步实现、ABI Fingerprint 的具体 C++ API/Loader 接线、Provider 匹配 API、完整 Hot Reload、Plugin Manifest/Packaging。身份/版本、依赖绑定和操作后置条件的进一步冻结见 [Module API v1 第 8 节](module-api.md#8-冻结范围与后续-apiabi-门槛)。ABI 配置字段、Canonical SHA-256 和严格相容/校验顺序见 [ABI Profile / Fingerprint v1](../foundation/abi-profile-fingerprint.md)（Frozen）；动态发现入口布局、具体 Public API 和实现仍未冻结。

本文件不声称已经实现 ModuleManager、真实动态装载、静态链接 Catalog 或跨平台验收；仍须以第 9 节的验证矩阵进行实现检查。
