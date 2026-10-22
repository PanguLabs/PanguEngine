# Pangu ModuleDescriptor & Registration API v1

> 状态：**Partially Frozen**。第 8 节列出的行为契约已冻结；C++ 代码块仍为 API 提案，**Public Header、具体签名/布局、导出名、ABI 与生产 Loader 实现未冻结**。
> 更新：2026-10-22
> 前置已冻结：[Module Registration & Lifetime v1](module-registration-lifetime.md)、[Service v1](service.md)、[ABI Profile / Fingerprint v1](../foundation/abi-profile-fingerprint.md)、[Memory](memory-management.md)、[Build System](../foundation/build-system.md)
> 决策：[ADR-0010：统一 C++23 Module 入口](../decisions/0010-unified-cpp-module-entry.md)、[ADR-0011：Module API v1 行为契约](../decisions/0011-module-api-behavior-contract.md)。冻结行为不代表实现或跨平台 ABI 已验收，现有证据见第 9 节。

## 1. 设计目标与边界

一个 Module 仍对应唯一 CMake STATIC 或 SHARED Library Target。Plugin 是分发/启用单元，Module 是代码 Owner；Host / Context / World 是实例 Owner。不得引入 `BinaryImage` 架构层、统一 `IModule` / `IService` 虚基类、`ExtensionDescriptor` / `ExtensionManager` 或隐式全局 Service Locator。

- **STATIC**：编译/生成阶段显式引用每个 Module 的 C++ 登记函数，Catalog 是链接器可见的根。
- **SHARED**：OS Load 前按 ABI v1 执行非执行式预检；OS Load 后通过固定的版本化 `extern "C"` 导出符号获取同一原生 C++23 ModuleDefinition，完成二次数据核对并调用统一 C++23 注册函数；前提是 OS Load 和 Query 之前完成可靠的候选产物绑定与受控 C++ ABI/Public API 预检。LoadLibrary/dlopen 可能执行初始化器，Discovery 不能当成未执行代码的安全边界。
- 两种来源最终创建相同的、由 Host 持有的 `ModuleRecord`（仅内部实现概念），走同一依赖、注册事务、激活、撤销和 CodePin 协议。
- **Descriptor 不存运行时状态**。动态库句柄、外部 Pin、Task、实例 Borrow、故障报告等属于 Manager/Scope Owner。
- **注册能力与 Service 实例 Ready 分离**；Concrete Service 不强制注册 Registry。

## 2. 身份与描述数据

`ModuleId` 是**明确分配并持久保留的 128 位强类型身份**，与显示名称、路径、PluginId、ErrorDomainId、ServiceId 无关。不使用 `std::hash`、RTTI、模块地址或加载序号生成身份。创建逻辑 Module 时一次分配并纳入源码管理；配置、重建、改名或移动目录不得重新生成，复制为新的逻辑 Module 时必须分配新 ID。由 CMake/PanguMeta 的受控元数据或明确声明提供 ID，不要求每个 Module 拥有 `Module.toml`；生成工具的具体 API 不在冻结范围内。

身份按完整 16 字节比较，全零无效；持久文本固定为按字节数组顺序编码的 **32 个小写十六进制字符**，拒绝长度错误、非规范字符和零值，不使用平台 GUID 的内存字节序。这冻结的是身份表示规则，不冻结 C++ 对象布局或哈希容器实现。

`ModuleVersion` 使用三个 `uint32_t` 表示非负整数 `major.minor.patch`，`0.0.0` 合法，与构建身份独立。v1 **只允许同一受管装载域中一个 ModuleId 有一个生效版本**；依赖只接受三个分量精确相等，不支持范围、通配符或隐含的同 Major 兼容性。

建议的原生 C++23 形状（类型布局、签名均为待审查）：

```cpp
namespace pg::module
{
struct ModuleId
{
    std::array<std::byte, 16> Bytes{};
};

struct ModuleVersion
{
    std::uint32_t Major{};
    std::uint32_t Minor{};
    std::uint32_t Patch{};
};

enum class DependencyKind : std::uint8_t
{
    kRequired,
    kOptional,
};

struct ModuleDependency
{
    ModuleId Id{};
    ModuleVersion Version{}; // v1 精确匹配；可选依赖仍需要显式绑定
    DependencyKind Kind{};
};

struct ModuleDescriptor
{
    ModuleId Id{};
    ModuleVersion Version{};
    std::uint32_t ProtocolVersion{1};
    pg::AbiFingerprint AbiFingerprint{};  // 由实际构建生成，不准手填
    std::string_view CanonicalAbiProfile; // 同次构建生成的 Canonical JSON，供验证及字段差异报告
    std::span<const ModuleDependency> Dependencies;
    std::string_view DisplayName;   // 可选诊断用途，非身份
    std::string_view BuildIdentity; // 非 ABI Fingerprint
};

class RegistrationContext;
struct StaticModuleCatalog;
using RegisterModuleFn = pg::Status (*)(RegistrationContext&) noexcept;

struct ModuleDefinition
{
    ModuleDescriptor Descriptor{};
    RegisterModuleFn RegisterModule{};                // 必填；默认空值不是合法的 ModuleDefinition
    const StaticModuleCatalog* LinkedStaticModules{}; // SHARED 承载的 STATIC 项；字段形状未冻结
};

using QueryModuleFn = const ModuleDefinition* (*)() noexcept;
} // namespace pg::module
```

`pg::Status`、`pg::AbiFingerprint` 的具体 C++ API 尚未冻结，此处**不是可独立编译的 Public Header**。注册入口不使用异常；回滚、失败报告由 Host 所有的操作上下文负责。

**入口有效性**：STATIC / SHARED 使用同一验证规则：Query 入口、Query 返回的 `ModuleDefinition*` 和其中的 `RegisterModule` 均须非空，否则在进入注册前返回定义无效错误。无需登记任何能力的 Module 仍提供显式返回成功的空操作注册函数，不以空函数指针表达该语义。非空检查不代替 ABI、接口契约和代码驻留校验。

**数据生存期**：上述 `span/string_view` 只在取得有效 CodePin 的查询/登记窗口内借用。ModuleManager 必须复制 ModuleId、版本、依赖列表、必要字符串和报告到 Host 拥有的存储，然后才能让这些记录跨卸载继续存在。不得保存指向候选动态库段的悬空借用。

### 2.1 依赖选择与阶段要求

Host 从显式提供的候选集合解析依赖，不按路径扫描顺序或“最后加载者”选择。Required 缺失或版本不匹配时拒绝登记；Optional 缺失、版本不匹配或未被显式选中时保持未绑定，并提供结构化原因。即使候选存在且版本匹配，也不得隐式绑定 Optional。调用方显式要求绑定时，绑定失败须返回错误，不能静默降级为未绑定。

依赖边在本次 Module 登记前确定；成功登记后不自动补绑、替换或切换版本。Optional 一旦绑定，与 Required 一样取得所需驻留保护、参与有效生命周期图校验并限制卸载。新增候选不会改变已有绑定；变更绑定须完成依赖方的受管卸载并重新装载。重复依赖 ID、自依赖和已绑定图中的环在发布前拒绝。

| 阶段 | 对已绑定依赖的要求 |
| --- | --- |
| Load / 发现 | Host 可先收集完整候选及身份，不把“已发现”当作可调用；单个 Load 不隐式扫描或递归装载未知 Module |
| Register | 依赖先 Registered 或 Active，能力与代码有效；新绑定拒绝 CleanupPending / Unloaded 记录，即使身份仍被占用 |
| Activate / 新 Scope 发布 | 依赖 Module 为 Active；具体 Service/工作还须满足对应 Scope 的 Ready 和实例依赖条件 |
| 停止 / 清理 | 先处理依赖方，保留其已持有的代码与实例保护直至清理完成；不能为了满足 Active 前置条件重新开放已关闭的业务入口 |

## 3. STATIC Catalog

由 CMake/PanguMeta 生成 **一个显式 Catalog 装配源文件**，加入 Host/最终链接目标，而不是生成第二个 Module Library。每个 Module 本身仍只有一个 STATIC/SHARED 目标。

```cpp
namespace pg::module
{
struct StaticCatalogEntry
{
    QueryModuleFn Query{}; // 同一原生 C++23 Query，直接链接保留
};

struct StaticModuleCatalog
{
    std::span<const StaticCatalogEntry> Entries;
};
} // namespace pg::module
```

Catalog 构造函数/数组必须直接引用各静态 Module 的显式查询入口，确保静态归档中的相应对象文件被链接器拉入，不依赖业务全局构造、静态初始化顺序或对所有 Module 无差别 `whole-archive`。其生成行为由**最终链接目标**决定，不把 Catalog 当成 Module 的另一个物理库。

`StaticCatalogEntry::Query` 指向代码的实际承载者：若静态代码装入可卸载的动态库，CodePin 保护该动态库；单独 STATIC 归档不执行 OS 卸载。按 ABI v1，在最终链接/装配阶段验证参与 C++ 互操作的 STATIC Module Profile，不能把成功链接当成 ABI 相容证明。Host 在登记阶段把 Descriptor 复制进自身内存，并检查重复 ID、依赖和版本；Profile/Digest 的有效性与一致性同样必须验证，不能仅比较声明的 Fingerprint。

**动态承载库内的 STATIC Module**：每个最终承载目标生成自己的完整、扁平 STATIC Catalog。EXE 的 Catalog 在宿主启动时显式交付；SHARED S 的唯一 Query 返回 S 的定义，并通过原生 C++ 数据关联提供其承载的全部 STATIC 条目（上文 `LinkedStaticModules` 为字段提案）。叶子 STATIC 定义不递归携带 Catalog；Catalog 不包含承载者自身，也不包含另一个 SHARED 的条目。不再导出第二个发现符号。

例如 S 链入 STATIC A/B，Host 通过 S 的一次入口定位发现 S/A/B，分别验证并预留三个 ModuleId、建立独立生命周期记录，再按依赖顺序注册。装载成功须让 Host 取得或查询该组全部逻辑 Module 的句柄，不能只返回 S 而留下无法驱动注册/清理的 A/B。整组身份预留对竞争装载不可半完成；出现重复 ID 或非法条目时不发布其中任何一项，也不交付部分成功句柄，并回滚本次取得的所有预留及 OS 持有。该组元数据、接口和代码的预检必须覆盖实际承载的全部 Module。

S/A/B 的记录和 CodePin 都关联同一个真实 OS 承载句柄。停止某个逻辑 Module 只退出该记录的业务生命周期；只有该承载库的全部受管记录、清理和代码保护结束后才允许 OS Unload。逻辑 Module 的停止不默认级联停用同库其他 Module；Host 关闭整个承载库时协调其全部记录。内部承载账本不构成新的 BinaryImage 架构层。

`Unloaded` / `TryFinishUnload` 成功表示该逻辑 Module 已清理完毕并释放其承载持有，不能据此断言同库代码已从进程移除。只要有其它记录或保护仍需要该库，Manager 继续持有实际 OS 句柄。已退出记录可以释放身份占位；重新登记仍须使用原承载代码和受控候选。若要加载替代产物，必须先完成旧承载库全部记录的清理与释放，不能用单个逻辑身份释放绕过产物绑定或引入多代际并存。最后一个持有释放时若实际 OS 卸载未完成，则由该次清理保留记录/身份和必要句柄并返回未完成，不能仅清空账本就报告成功。

## 4. SHARED Discovery：固定符号定位 + 原生 C++23 Query

**已确认技术路线：STATIC / SHARED 共享同一种 `pg::module::ModuleDefinition` 和原生 C++23 注册函数签名。SHARED 不再采用独立的 C POD Discovery、C 状态码协议或 `void*` Registration Bridge。** 仅动态库的入口定位方式不同。

建议固定查找符号 `PanguModuleQueryV1`（名称与平台导出细节尚待验证）；`extern "C"` **只确保可通过固定名称查询该符号**，不是在声明 C 数据交换协议，不意味着返回的 C++ 对象跨不兼容 ABI 仍安全。动态库内的生成包装函数转调其原生 C++ Query；STATIC Catalog 可直接引用对应原生函数：

```cpp
namespace pg::module
{
// 引擎 C++23 内部使用上文统一定义的 QueryModuleFn 类型。
// 以下由每个 Module 的生成代码提供；函数名按 Module 唯一化。
const ModuleDefinition* QueryExampleModule() noexcept;
} // namespace pg::module

// SHARED 才导出；生成器使该符号对应当前唯一 Module Target。
// PG_EXPORT 为平台可见性示意，不是反射标记宏。
extern "C" PG_EXPORT const pg::module::ModuleDefinition* PanguModuleQueryV1() noexcept
{
    return pg::module::QueryExampleModule();
}

// STATIC Catalog 条目：&pg::module::QueryExampleModule
```

这里 `QueryExampleModule` 和 `PG_EXPORT` 都是**非冻结示意名**，不是要求业务作者手写包装宏。链接器函数地址与平台动态符号解析所得函数指针的调用约定、Language Linkage 和目标 ABI 匹配，必须在 Windows / Linux / macOS 实测；不能仅凭 `reinterpret_cast` 或符号名称推断兼容性。

**SHARED 校验和注册顺序：**

1. **Build-Time**：由实际 CMake/Clang 配置生成 Module 的完整 Canonical ABI Profile、Fingerprint、Module 协议版本及其与构建产物绑定的预检数据；不能由用户声明“兼容”。
2. **Preflight（OS Load 之前）**：不执行候选模块的代码，读取并验证与**将要加载的实际产物**绑定的元数据，完成 Host/Candidate 的 Profile、Fingerprint、版本化 Query/ModuleDefinition C++ 接口契约及必要平台校验。预检材料无法验证、产物无法绑定、ABI 或 Public Module API Contract 无法确认时，**Fail Closed，禁止 OS Load 和 C++ Query**；普通可替换 Sidecar 不能作为充分证明。预检与真正 OS Load 间替换文件的 TOCTOU 问题必须由后续装载机制解决。
3. **OS Load**：由 ModuleManager 取得并维护真实动态库句柄；`LoadLibrary/dlopen` 可能先执行 DLL 初始化器及全局构造，因此 Preflight 不等于不可信代码沙箱或签名认证。
4. **Symbol Lookup / C++ Query**：只在 Preflight 通过后，用 `GetProcAddress/dlsym` 等定位固定版本符号，按**已确认兼容**的 C++ 函数类型调用，获得 `const ModuleDefinition*`；返回空值/失配视为失败。Query 只能返回有效、在代码驻留期稳定的定义视图，不得通过静态构造完成业务注册。不得在 Preflight 前解引用模块提供的 C++ 对象。
5. **Post-Load Recheck**：在持有有效代码驻留保护时，验证返回的 Descriptor 完整 Profile/预生成 Canonical JSON/Fingerprint、协议版本、ModuleId、依赖与预检数据一致（包含解析、规范化、长度上限与 Digest 重算）；再检查 Public Interface Contract、冲突和依赖图，并按第 2 节拒绝空注册入口。Fingerprints 一致不证明 Public API/ODR，也不是签名。Descriptor 的借用 `span/string_view` 必须在卸载前复制到 Host 存储；任意失败均不进入注册。
6. **Native C++ Registration**：对 STATIC 与 SHARED 一律由 ModuleManager 调用 `ModuleDefinition::RegisterModule(RegistrationContext&)`，返回 `pg::Status`（具体类型仍待 ADR-0007 API 冻结），使用同一事务、Scope/Owner、Error 报告、回滚与 CodePin 规则。无需转换 C 错误码或调用单独注册 Bridge。

**数据与生命周期约束**：模块查询入口仅定位/返回元数据，不负责初始化业务实例。Query/注册入口和回调的所在代码必须驻留；引用字段和析构/分配器来源受 Memory Contract 约束。Module 卸载先撤销业务与在途调用、排空 Scope Borrow，最后释放实际动态库。静态 Module 不进行 OS Unload。

**冻结范围区分**：**统一 C++23 ModuleDefinition/Registration + SHARED 稳定 C-linkage 符号定位** 的机制保持 ADR-0010 约束；`PanguModuleQueryV1` 的最终名称、`ModuleDefinition` 的 Public 类型布局/接口契约、载入前可信产物绑定工艺、导出调用约定、错误及并发实现仍属 Draft/待验证，不宣称跨平台已验收。

## 5. RegistrationContext：声明与提交分离

`RegistrationContext` 属于当前正在注册的 Module、Scope 的受控事务，**不是全局 ServiceLocator**。业务 Module 的登记入口只提交声明，不直接向外部发布 Service：

```cpp
namespace pg::module
{
class RegistrationContext
{
public:
    template <class TContract, class TFactory>
    [[nodiscard]] pg::Status DeclareContractProvider(pg::service::Scope scope, TFactory&& factory) noexcept;

    template <class TRegistry, class TContributionFactory>
    [[nodiscard]] pg::Status DeclareContribution(pg::service::Scope scope, TContributionFactory&& factory) noexcept;

    template <class TWorkFactory>
    [[nodiscard]] pg::Status DeclareRuntimeWork(pg::service::Scope scope, TWorkFactory&& factory) noexcept;
};
} // namespace pg::module
```

此处模板仅表示**强类型方向**；具体约束、Factory 所有权、键/Provider ID、类型擦除点、无异常内存失败策略依赖 Service API 的进一步设计，尚不能直接固定签名。对于已确定的 Concrete Service，Scope Owner 直接构造/注入，可完全跳过动态 Registry。

`pg::service::Scope` 只表示 Host / Context / World **种类**，不标识具体实例。Module 级声明可记录后续装配策略；向 Registry 实际安装时，Host 必须将事务显式绑定到具体 Scope Owner 及其所属 Host/Context，并校验 Scope 方向。不得仅凭 `Scope::kContext` 或 `Scope::kWorld` 选择任意实例，或隐式向全部实例发布。具体目标 Scope 的句柄/上下文签名仍待 Service API 评审。

**一次声明，多次装配**：每次成功 Load 的生命周期至多执行一次 Module 注册回调；重复 Register 前置拒绝。注册产物是 Host/Manager 持有且有代码保护的可重复装配声明。Provider、Contribution 和运行工作均通过工厂/配方在具体 Scope 创建状态，不把一个只能消费一次的可移动对象当成多 Scope 的实例来源。工厂本身可以仅移动，但其调用契约必须支持多次装配；可共享的只读状态须显式声明所有权，可变实例及撤销 Token 按 Scope 独立持有。

Module 的 Register/首次 Activate 与之后的 Scope 装配是不同操作：Module Active 后，Host 为新增 Context/World 执行单独的装配事务，不重跑整个注册回调或重新 Activate Module。新 Scope 失败只撤回该次装配的实例、贡献和工作；其在途清理由该 Scope 的操作记录持有，既有 Scope 和 Module 保持原状态。Scope 装配结果同样独立给出错误和清理进度，不能把局部失败映射成全 Module 的 CleanupPending。

进入 Module Quiescing 时同时关闭新的 Scope 装配入口。装配与停用必须有确定的线性化顺序：停用前已接纳的装配由 Host 追踪，若尚未发布则撤销并清理，停用后不得再发布；Module 卸载汇总其所有已发布和未完成装配记录及保护。初次 Activate 失败则按第 6.1 节清理该 Module 初次激活关联的全部装配，并保持业务不可见。

事务规则：

- `RegisterModuleFn` 可以声明 Module 能力、工厂及后续 Scope 装配策略；**不能擅自创建全部 Host/Context/World 实例**。
- 登记先暂存；ModuleManager/Host 验证同目标 Scope 的唯一 Provider、业务 Registry 的冲突规则、Module/Service 两套依赖图。具体 Provider 绑定后还须检查合并生命周期约束无环且顺序不矛盾，不能只验证两张图各自无环；通过后再**同一 Scope 原子发布**。跨多个 Scope 依靠 Host 可见性闸门协调，不虚构全进程原子提交。
- 撤销 Token/Journal 由 ModuleManager/相关 Registry 所拥有；用户登记回调不直接执行全局 `Commit()` 或持久存储可卸载 DLL 内数据视图。
- 失败按登记事务及实际实例完成顺序逆序回滚，保留首个 Error 及有界次要清理报告。
- Registry Contribution 的撤销先拒绝新查找/调用，待在途回调结束后才能销毁插件拥有的状态；`ModuleCodePin` 和实例借用保护始终分开。

### 5.1 可选运行工作与停止协调

`DeclareRuntimeWork` 是可选装配声明的 API 提案，用于不适合表达成 Service/Contribution 的持续任务；注册时只保存工厂和生命周期策略，不启动任务。Host 在具体 Scope 装配时创建工作状态，并在允许其启动之前记录 Owner、依赖、清理入口及 CodePin/实例保护。没有 Service 的 Module 也可以声明 Host 范围工作，其运行状态仍归 Host 所有，不放进 `ModuleDefinition` 或全局变量。冻结的是追踪、停止和排空行为，不是该模板方法的名字或签名。

这类工作采用以下停止协议，具体 Concept、函数签名和类型擦除布局仍待设计：

- **启动**：显式可失败；Host 在调用启动逻辑前就持有可清理的状态。启动部分完成后失败，也须纳入同一回滚流程。
- **请求停止**：非阻塞、`noexcept`、可重复调用；关闭工作自身的新任务/回调来源，并向已有任务传递停止请求。停止请求已发出不代表任务已经完成。
- **检查完成**：非阻塞地报告尚未结束、已结束或清理错误；“已结束”须覆盖在途任务、回调及其所访问的上下文。错误或超时不能伪装成已结束。
- **最终销毁**：只在该工作已结束且外部使用已排空后执行，随后释放其依赖 Borrow 和代码保护。Scope Owner 负责且只执行一次最终销毁；需要返回错误的收尾先通过显式完成检查处理。

ModuleManager 通过 Host 协调器找到各 Scope 的工作记录，按有效依赖逆序逐项请求停止、检查完成并销毁。被依赖方的工作和实例须保持依赖方清理所需的可用性，不能无差别提前停止整个依赖闭包。`BeginUnload()` 关闭业务闸门并安排停止流程，`TryFinishUnload()` 推进尚未完成的停止和排空；已有 Host 任务调度机制负责继续执行在途任务。

Concrete / Contract Service 和 Registry Contribution 已有自己的停止与清理协议时，由 Scope Owner 接入同一协调流程，不重复登记第二份工作所有权，也不强制实现统一生命周期基类。任何持续工作都必须在启动或入队前完成追踪和驻留保护，不允许用未登记的后台线程绕过卸载协议。Host/Registry 锁内不得调用未知启动、停止、完成检查或析构回调。

Registered 装配阶段及 Activate 失败均使用上述协议收尾，不能因 Module 尚未 Active 而跳过已启动工作。若工作不响应停止或仍有合法借用，则保留状态、依赖和库句柄并返回未完成；Host 继续存活以推进清理，不能强制卸载。

## 6. ModuleManager / ModuleCodePin

```cpp
namespace pg::module
{
struct ModuleHandle;            // 强类型、非拥有句柄；必须防止复用后的 ABA
struct ModuleLoadCandidate;     // 已发现的 STATIC 项或 SHARED 产物
struct ModuleLoadResult;        // 根 Module 与本次接纳的全部逻辑 Module 句柄；具体容器/布局未冻结
struct ModuleOperationReport;   // 操作入口拥有；交付后独立于 Host/Module 存活的有界诊断
struct ModuleOperationProgress; // 独立表达调用后的状态/清理责任，不从 Error 推断阶段

class ModuleCodePin
{
public:
    ModuleCodePin(const ModuleCodePin&) = delete;
    ModuleCodePin& operator=(const ModuleCodePin&) = delete;
    ModuleCodePin(ModuleCodePin&&) noexcept;
    ModuleCodePin& operator=(ModuleCodePin&&) noexcept;
    ~ModuleCodePin() noexcept;
};

class ModuleManager
{
public:
    [[nodiscard]] pg::Result<ModuleLoadResult> Load(const ModuleLoadCandidate&, ModuleOperationProgress&,
                                                    ModuleOperationReport&) noexcept;
    [[nodiscard]] pg::Status Register(ModuleHandle, ModuleOperationProgress&, ModuleOperationReport&) noexcept;
    [[nodiscard]] pg::Status Activate(ModuleHandle, ModuleOperationProgress&, ModuleOperationReport&) noexcept;
    [[nodiscard]] pg::Status BeginUnload(ModuleHandle, ModuleOperationProgress&, ModuleOperationReport&) noexcept;
    [[nodiscard]] pg::Status TryFinishUnload(ModuleHandle, ModuleOperationProgress&, ModuleOperationReport&) noexcept;

    [[nodiscard]] pg::Result<ModuleCodePin> TryPinForBusiness(ModuleHandle) noexcept;
};
} // namespace pg::module
```

`ModuleHandle` 建议为带代际的强类型不拥有句柄，不得携带裸 `ModuleRecord*` 供任意业务长期访问。Manager、其控制块及 OS 库句柄是驻留所有者；`ModuleCodePin` 为独立 Move-only RAII 代码保护，**不隐式延长 Service 实例生命期**。

- `TryPinForBusiness` 仅在 Module 为 Active 且业务闸门开放时成功；Loaded / Registered 阶段不得发新业务 Pin。注册、装配和清理使用协调器内部权限，不借此提前开放业务访问。取得业务 Pin 后，访问 Service 仍须满足对应 Scope 实例 Ready 条件，并有 Borrow/Lease 或等价的 Scope 所有权及销毁顺序保护。
- `TryPinForBusiness` 和 `BeginUnload` 的 Quiescing 闸门必须有确定的线性化点；开始 Quiescing 后不再发新业务 Pin，也不能通过重新 Activate 绕过尚未完成的清理。
- 之前获得的 Pin、合法借用以及内部清理所需的权限可以在 Quiescing / Draining 使用；**内部 Cleanup Pin 不作为公共接口开放**。
- Manager 持有的基本代码所有权不算外部 Pin；卸载时不可等待自身清理所需的内部 Pin 归零之后才开始 Destroy。
- 先 Drain Task/Callback、释放外部使用，再沿合并生命周期约束的逆序逐项销毁 Service/资源，释放依赖 Borrow，最后撤销贡献、释放 CodePin 和卸载真正的动态代码承载库；不能等待整个依赖闭包的全部 Borrow 先归零。
- `TryFinishUnload` 遇到 Busy / Timeout 必须保留原 ModuleId 占位、ModuleRecord 和真实库句柄，不能假装成功，也不能趁间隙加载第二份同身份 Module。

错误语义保持 ADR-0007：可恢复失败使用 `Result/Status`，独立有界 `ModuleOperationReport` 存放加载、回滚、卸载细节；报告由操作入口拥有并显式向下传递，交付后其文本、存储和析构机制必须独立于 Host、Diagnostics 和原始 Module 存活。Host 拥有元数据副本不代表已交付报告可以借用 Host 分配域；不强制立即日志，也不依赖插件字符串在卸载后仍存在。

### 6.1 操作后置条件与继续清理

下表冻结操作的后置条件，不冻结 Public 状态枚举或方法签名。`CleanupPending` 表示业务闸门已关闭、已进入 Quiescing / Draining 或后续清理阶段，尚未完成受管卸载；该名称不是新增业务活跃阶段。对同一 Module 的管理操作由 Host 串行协调，冲突调用在产生副作用前拒绝；这不代替业务 Pin 与 Quiescing 之间的同步规则。

所有操作先校验适用的句柄代际、前置状态和依赖条件；**前置拒绝**保持原状态，不启动回滚。`Register` / `Activate` 一旦开始执行后失败，统一进入 `CleanupPending`，不自动恢复到可重试的 Loaded / Registered。原始 Error 可以来自任意下层，不能据其错误域或代码推断本次调用是否已经产生副作用。

因此操作必须独立交付 `ModuleOperationProgress`：至少区分前置拒绝、目标操作完成、清理待完成和失败后的清理已完成，并说明本次调用后句柄是否仍有效以及由调用方继续收尾还是由 Manager 接管。上述参数是传递方式的提案，字段布局和最终签名仍未冻结；冻结的是**控制流进度与原始 Error、可截断诊断报告分离**。每条返回路径都初始化进度，报告截断不影响调用方决策；操作返回后不得保留调用方进度对象的引用。成功的 Load / TryFinishUnload 分别交付新有效句柄 / 使原句柄失效；Load 失败不交付成功句柄，即使 Manager 仍在清理。

| 操作 | 前置条件 | 成功后的状态 | 执行失败或未完成后的状态与责任 |
| --- | --- | --- | --- |
| `Load` | 候选可验证且身份未被占用；Manager 在装载前预留身份 | 本次接纳的逻辑 Module 均 Loaded；交付根及全部有效句柄 | 不交付成功句柄；Manager 撤销本次整组装载。无法立即完成清理时由 Manager 保留记录、身份占位及必要库句柄并负责继续清理，调用方不能凭失败结果发起注册 |
| `Register` | Loaded；已绑定依赖满足第 2.1 节阶段要求 | Registered；能力仅供协调器装配，业务仍不可进入 | 返回原始注册 Error；撤回本次事务的可见性并安排逆序回滚，保留有效句柄为 `CleanupPending`；调用方继续 `TryFinishUnload`，不能直接重试 Register |
| `Activate` | Registered，依赖 Module 已 Active，初次目标 Scope 的装配条件可满足 | Active；具体 Service 仍须满足自身 Ready 条件 | 返回原始激活 Error；关闭相关业务入口并安排已创建实例/工作的逆序清理，保留有效句柄为 `CleanupPending`；调用方继续 `TryFinishUnload`，不能直接重试 Activate；后续新增 Scope 不调用本操作 |
| `BeginUnload` | Loaded / Registered / Active，或已经为 `CleanupPending`；仍有未清理完成且需要本 Module 的依赖方时先拒绝，由 Host 按依赖顺序停用 | `CleanupPending`；成功仅表示已关闭入口并安排停止，不代表已经卸载 | 前置拒绝不改变状态；进入清理后发生的停止错误保留在报告中并维持 `CleanupPending`。清理期间重复调用不重复创建清理任务，也不重新开放业务 |
| `TryFinishUnload` | `CleanupPending`，包括注册/激活失败后自动进入的清理 | 逻辑 Module 为 Unloaded；释放身份占位，原句柄失效；同库实际 OS 卸载还须满足第 3 节条件 | Busy / Timeout / 可恢复清理错误均保留有效句柄、身份及必要保护；修复条件或等待进展后重复调用，不重复销毁已完成清理的对象 |

`Register` / `Activate` 执行失败时，即使资源回滚已能立即完成，也保留受管记录和原句柄直到 `TryFinishUnload` 确认收尾；此后需要重试业务操作时，从新的 `Load` 开始。对已经失效的句柄调用管理操作返回失效句柄错误，不把它误当成新的 Module，也不再次执行卸载。

**未交付句柄的装载失败**：Manager 将待清理记录纳入 Host 的清理推进机制，在正常运行和 Host 关闭时持续检查并重试可恢复的清理；不得依赖调用方持有一个未返回的句柄。未完成时继续拒绝同身份装载；Host 关闭必须报告未完成并保留 Manager/控制块，不得用 Manager 析构强制释放库。内部记录的标识、清理调度和状态查询接口仍待实现设计，但必须能让 Host 观察未完成项及失败原因。

**跨调用错误与报告**：Register / Activate 的原始 Error 不被后续 Busy 或清理错误覆盖；Manager 保存必要的有界主错误和清理进度，各次 `TryFinishUnload` 返回当前清理操作的 Status，并在本次报告中关联原始失败及追加次要诊断。操作返回后不得继续借用调用方传入的 `ModuleOperationReport&`；异步清理使用 Manager/Scope 自有的有界上下文，在后续调用或 Host 收尾时交付快照。`TryFinishUnload` 成功只表示清理完成，不把先前注册/激活失败改写成成功。

## 7. 最小实施与验收切片

按相互依赖的最小切片验证，不做第二个构建系统：

1. **Identity/Descriptor**：静态编译期常量与 Host 深复制，拒绝重复 ID、非法版本/缺失依赖；卸载后仍可读取已复制元数据，报告在 Host 销毁后仍可读取及释放，不保留原 Module 的字符串视图。
2. **STATIC Catalog**：两个真实 STATIC Module，通过生成 Catalog 根引用入 Host；链接器裁剪下注册入口仍被拉入，不依赖全局构造。
3. **Registration**：暂存 → 冲突检查 → 同 Scope 提交；第 N 项失败回滚前 N-1 项并保留原始 Error，Concrete Service 不强制入 Registry；具体 Context/World 独立安装，多 Scope 装配不可半可见；两张图各自无环但合并有环时仍拒绝发布/激活。
4. **SHARED**：真实 `LoadLibrary/dlopen` + 一个稳定名称的 `extern "C"` 导出符号（返回原生 C++23 ModuleDefinition）；必须先验证预检数据与将加载产物的绑定、ABI Profile 与 C++ Public API Contract，再允许 Query；非法 Profile、Profile/Digest 不一致、预检与装载后数据不一致均拒绝注册；确认 DLL 初始化可能先执行，并验证元数据复制与安全卸载。
5. **Pin/Drain**：外部 Pin 与 Task/Borrow 阻止卸载；Loaded / Registered / Quiescing 拒绝新业务 Pin，Active 仍检查 Service Ready/Borrow；Busy 保留代码；依赖逐个逆序销毁、不因内部 Borrow 自锁。
6. **平台矩阵**：Windows、WSL Linux、macOS 做本机真实链接/加载验收；Android/iOS 首先做目标编译/链接及入口布局检查，设备运行不在本切片内。

针对上述接口契约还须覆盖：

- **空入口**：STATIC / SHARED 的空 Query、空定义和空注册函数均在注册前拒绝；显式空操作注册函数正常通过。
- **失败后状态**：前置拒绝保持原状态；Register / Activate 执行失败后句柄只可用于清理，不允许直接重试；清理完成后句柄失效，新 Load 才能重新占用身份。
- **激活失败且回滚 Busy**：先启动一项工作，再使后续装配失败；原始 Error 保留，第一次 `TryFinishUnload` 返回 Busy 时工作、依赖及库仍存活，工作结束后再次调用成功，销毁只执行一次。
- **无 Service 的持续工作**：Host 范围工作收到停止请求后才结束；验证 BeginUnload 与完成分离、重复停止安全，以及 Registered 装配失败也会请求停止。再验证依赖方清理期间被依赖方的必要工作仍可用。
- **Load 失败且清理延后**：调用方未获成功句柄时，Manager 仍能推进清理；完成前重复身份被拒，Host 关闭可观察未完成项，完成后身份释放。
- **报告生命周期**：原调用报告销毁后，异步清理仍安全；后续报告保留主错误与次要清理诊断，成功收尾不改变原操作的失败结果。
- **错误与进度独立**：前置拒绝和业务回调返回完全相同的 Error 时，前者不改变状态，后者明确进入 CleanupPending；报告缺失/截断也不影响区分。
- **身份/依赖**：规范文本与 16 字节往返、零值/非法字符拒绝、版本任一分量不符不匹配；Optional 未选中不绑定，绑定后形成环或卸载 Provider 时拒绝；清理中的依赖不能新绑定。
- **多 Scope**：Module 只注册一次；A 已运行时 B 装配失败只清理 B；随后可装配 C，A/C 的状态和撤销记录独立；装配与卸载争用不得在闸门关闭后发布。
- **同库多 Module**：SHARED S 内嵌 STATIC A/B，单 Query 可达全部条目；裁剪不丢条目，重复 ID 导致本次整组接纳失败；A 的代码保护及尚未退出的 B 阻止真实 OS 卸载。

临时 C++23 探针统一位于 `.probe/`，本次验证使用 `.probe/module-api-freeze/`；正式测试一旦引入仓库 `Tests/`，使用 CMake 4.0+ / CTest，不引入 Python 正式测试。

## 8. 冻结范围与后续 API/ABI 门槛

本轮按 [ADR-0011](../decisions/0011-module-api-behavior-contract.md) 冻结以下**行为契约**。第 7 节是实现验收要求，不表示整张矩阵已经通过；C++ 代码块用于表达方向，不作为可直接发布的头文件。

| 已冻结行为 | 权威章节 | 仍未冻结的实现/API 内容 |
| --- | --- | --- |
| 128 位身份、全零无效、规范文本与永久保留；三段精确版本 | 第 2 节 | C++ 对象布局、ID 生成/审查工具和元数据文件格式 |
| Required/Optional 的显式绑定、固定绑定、依赖阶段条件 | 第 2.1 节 | 候选集合、绑定结果/原因和图存储的 Public 类型 |
| STATIC 根引用、同库 Catalog 可达性、独立记录与共同代码驻留 | 第 3 节 | Catalog 字段、生成器、承载账本、跨平台符号与布局 |
| Query/注册入口非空、预检后查询、二次复核后注册 | 第 2/4 节 | 导出符号/调用约定、ModuleDefinition Public Contract、可信产物绑定和 TOCTOU 工艺 |
| 一次 Module 注册、多 Scope 独立装配/撤销、局部失败隔离 | 第 5 节 | Factory Concept、键和 Provider 选择 API、类型擦除、Scope 句柄 |
| 运行工作启动前追踪、停止/完成分离、依赖逆序清理 | 第 5.1 节 | 停止协议签名、任务组/调度器接线、回调存储 |
| Active 业务闸门、代码与实例保护分离、Busy 保留 | 第 6 节 | Handle 代际编码、引用计数、锁/原子操作及并发实现 |
| 操作后置条件、进度与原始错误分离、继续清理与报告所有权 | 第 6.1 节 | Status/Result、Progress/Report 类型与 ABI、未交付句柄记录的查询接口 |

冻结 Public API/ABI 前还必须完成：可独立编译的公共头文件及上述依赖类型；同一契约的 STATIC/SHARED 接线；所有借用数据与报告预算/上限；无异常分配策略；五平台目标布局和调用约定验证；装载前真实产物绑定与装载后二次核对；并发线性化、失败注入和卸载回归。不得因本轮行为冻结跳过任何一项，也不要求恢复独立 C POD Discovery。

## 9. 本轮验证证据与限制

### 9.1 正式 Module Identity 代码起步（2026-10-22）

已在 `Source/Runtime/Module/` 创建第一个实际 CMake `STATIC` Target：`PanguModule`，并在仓库根目录建立最小 CMake 入口、将长期回归测试放在 `Tests/Module/`。这只实现已冻结身份/版本规则的一小部分，**不表示 `ModuleDefinition`、`RegistrationContext`、`ModuleManager`、`ModuleCodePin`、ABI Profile 采集或 SHARED Loader 已实现**。

- Public `Source/Runtime/Module/Public/ModuleIdentity.h`（公开 include 使用 `#include <ModuleIdentity.h>`）提供 `pg::module::ModuleId`（16 字节强类型，全零无效）和 `ModuleVersion`（三段整数，`constexpr` 比较）；无分配 `FormatModuleId` 支持 `constexpr` 和规范化的 32 字符小写十六进制。
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
