# Pangu C++23 开发原则与最佳实践

> 状态：Partially Frozen（C++23、工具链、无异常、命名空间、编译期优先及受控 ABI 的既有原则已确定；本文新增的领域级实践大多为 SHOULD，具体 API、调度与协议尚待子系统冻结）
> 更新：2026-10-21
> 规范配置：仓库根目录 [`.clang-format`](../../.clang-format) 和 [`.clang-tidy`](../../.clang-tidy)
> 关联：[总体架构](../architecture.md)、[工具链](toolchain.md)、[构建系统](build-system.md)、[ABI](abi.md)、[错误与诊断](error-diagnostics.md)、[内存所有权](../core/memory-management.md)

## 1. 定位与用语

本文件提炼 PanguEngine 在 C++23 下通用的**代码设计原则与评审准则**，不代替各领域的权威设计文档，也不把讨论中的伪代码提升为正式 API。术语 MUST / SHOULD / MAY 分别表示必须、应该和可以；具体语义与文档状态见 [wiki 索引](../README.md)。

PanguEngine 的工程取向：

> **Compile-Time First、Build-Time Validation、Zero-Overhead Abstraction、Type Safety、Data-Oriented、Explicit Ownership / Lifetime、Exception-Free；按领域需要采用 Data-Driven 与 Event-Driven。**

这是一组相互制约的**设计目标**，不是“所有代码必须模板化、完全确定性、零分配或零间接调用”的绝对保证。对可靠性、维护性和实现复杂度的权衡必须显式记录。

本文同时收录通用 C++ 实践与 Pangu 的项目约束。C++23 + Clang、无异常、flecs、Module 单库及 `pga` 是项目决策；Data-Oriented、Data-Driven 和 Event-Driven 是适用范围不同的设计取向，不能据此要求所有 C++ 模块采用同一种架构。

## 2. 已确定的基础契约

1. **C++23 + Clang**；五平台具体编译器和 C++ 标准库见 [Toolchain](toolchain.md)。不得把各平台理解为同一 C++ 二进制 ABI。
2. 引擎 C++ 命名空间以 `pg` 开头；自定义 Clang 属性使用 `pga` 属性命名空间。
3. 引擎不使用 C++ Exception 进行错误传播；对外失败行为见 [错误与诊断](error-diagnostics.md)。
4. **Compile-Time First**：能在类型系统、Concept、`constexpr` 或静态分发中安全合理确定的，不无故留给运行时查找。
5. **Zero-Overhead Abstraction**：抽象的额外成本必须可解释；声称消除成本需通过生成代码、Profile 或 Benchmark 验证。
6. **受控 C++ Module ABI 可传递 STL 类型**，不一刀切禁止；同时必须满足 ABI、Allocator、销毁代码驻留及借用生命周期契约。
7. 元数据声明不使用 `PG_CLASS` / `PG_PROPERTY` 等反射标记宏；统一 `pga` 方向见 [Reflection / Meta](../core/reflection-meta.md)。
8. **CMake 4.0+** 是构建底座，一个 Pangu Module 对应一个实际 STATIC/SHARED Library Target；不再把历史 `PanguBuild` 自研构建内核、`Module.toml` 或 `.pgmodule` 当作当前冻结要求。

## 3. Compile-Time / Build-Time / Runtime 决策

优先将**本质上不变**的约束放在开发工具或编译器能验证的位置；不要为追求静态化而提前固化真实的运行时选择。

| 问题 | 优先阶段 | 边界 |
| --- | --- | --- |
| 类型约束、重载、固定后端选择、常量和封闭类型集合 | Compile-Time | `concept`、`constexpr`、`if constexpr`、静态分发；`std::variant` 的候选类型集合静态确定，活跃分支仍可在运行时选择 |
| Module 依赖、目标工具链、ABI 采集、Meta 生成、Shader/Asset 构建处理 | Build-Time | 由 CMake 及相关工具链处理；生成结果和实际配置须一致 |
| 游戏状态、运行时世界、I/O、用户输入、设备能力、Plugin 发现和动态 Provider | Runtime | 按需要承担动态查找、调度、状态及生命周期成本 |

- **SHOULD** 用 Concept / Type Traits 表达模板契约，优先值语义和静态分发；`constexpr` 函数仍可在运行时执行，`consteval` 只用于确须强制编译期求值的场景。
- **SHOULD** 将可以预生成的 Reflection / Serialization 访问代码及元数据前移到 Build-Time；运行时确实需要编辑器检查、动态注册、脚本或网络数据时保留适当的动态能力。
- **MUST NOT** 将 Build-Time Validation 理解成消除对外部文件、网络、存档及运行时状态的校验。
- **SHOULD NOT** “Template Everything”：评估构建时间、二进制体积、错误诊断、调试体验、依赖暴露与跨 Module API 稳定性。
- **MAY** 在 Plugin、Contract Service、Editor 扩展或运行时配置的后端使用间接调用、类型擦除和动态注册。动态分发或类型擦除也可用于隔离依赖、稳定接口或控制模板实例化规模；须说明收益与成本，热路径按测量取舍，并遵守领域已冻结的静态接口约束。

## 4. Zero-Overhead Abstraction（零额外抽象开销）

**Zero-Overhead 不等于 Zero Runtime Cost、Zero Allocation 或 Zero Instructions。** 目标包含两方面：未使用的抽象能力不应引入额外运行时成本；使用抽象实现同样语义时，相对于高质量手写实现不应平白增加运行时成本。是否达到取决于平台、优化器、调用方式与实测。

- `std::span<const T>` 的动态长度视图通常只需指针和长度；它**不会**自动拥有数据，也不意味着迭代或访问无需执行指令。
- 模板、`concept`、强类型 ID 和 `constexpr` 有利于静态优化，但不保证每处 inline、SIMD 或完全无分支。
- Runtime 注册、Handle 解析、分配、同步、GPU Fence、任务调度都可能是**功能必需成本**，不能以“Zero-Overhead”之名删去正确性保护。
- Hot Path **SHOULD** 避免不必要的分配、锁、虚分发、格式化、哈希查询、I/O 和 CPU/GPU 同步；确有需要则 Profile 并记录理由。
- Hot/Cold Path 按实际调用频率、数据规模和延迟/吞吐预算划分，不能按模块名称判断；Editor 交互、批量 Import、Shader 编译与高频诊断同样可能有性能瓶颈。经确认的 Cold Path **SHOULD** 在满足正确性的前提下优先可维护性。
- 对接口的隐藏成本（分配、阻塞、I/O、资源同步、线程切换）**SHOULD** 在名字、返回类型或文档中明确，不能仅由调用者猜测。
- 设计顺序：**正确性与契约 → 数据访问模式 → 测量 → 定向优化 → 回归验证**。

## 5. Type Safety、值语义与现代 C++23

- 普通数据类型 **SHOULD** 优先值语义；有稳定身份或独占资源的对象应显式表达身份、借用或仅移动所有权。值语义不要求所有参数都按值复制：便宜复制的输入可按值传递，大对象的只读输入通常用 `const T&` 或受约束视图。
- **SHOULD** 在创建时初始化对象并建立有效状态；优先 Rule of Zero，由成员承担资源管理。自定义资源包装器须明确复制、移动和析构语义；无异常下可能失败的资源获取可通过返回 Result 的工厂表达，不能把半初始化对象当作成功结果发布。
- 用强类型 ID / Handle、`enum class`、带单位的值类型防止混用；ABI/网络/磁盘/GPU 格式使用明确位宽的整数，`std::size_t` 用于本机内存尺寸和容器索引。
- 用 `std::optional<T>` 表示正常的“不存在”，用 `std::expected` 表示操作可能失败；公共可恢复失败接口统一使用 `std::expected<T, Error>`，Module 内部可用领域专属错误类型，但须在公共边界转换，见[错误与诊断](error-diagnostics.md)。`std::variant` 适合候选类型编译期已知、活跃状态可在运行时变化的有限集合。
- 返回 Result/Status 的可失败函数 **MUST** 使用 `[[nodiscard]]`；其他重要返回结果 **SHOULD** 使用。`noexcept` 应与真实函数契约一致，不机械地全部加上。
- `std::string` 持有字符数据，`std::string_view` 只借用；`std::span` 借用连续内存，不延长底层容器或分配域的生命周期。
- 标准 Algorithms / Ranges **SHOULD** 用于清晰的数据操作；性能敏感循环可使用经过验证的直接迭代，避免为组合风格牺牲访问局部性。
- 避免隐藏的 `reinterpret_cast`、`const_cast`、`void*` 类型分发；低层 OS/GPU/Allocator/C 边界允许有约束的类型转换。
- C++23 **没有标准 `std::function_ref`**；非拥有 callback 可使用生命周期受约束的模板参数或明确的借用接口。需要可移动拥有型回调时考虑 C++23 `std::move_only_function`；`std::function` 仍可用，但要确认复制、分配和销毁成本。

强类型的示意（具体 Pangu 类型未冻结）：

```cpp
namespace pg
{
struct EntityId
{
    std::uint32_t Value{};
};

struct TextureId
{
    std::uint32_t Value{};
};
} // namespace pg
```

示例只展示类型区分与初始化，不规定 ID 位宽、零值含义或有效性校验协议。

## 6. Ownership、Lifetime 与 Memory

**权威约束：**[Memory Ownership Contract v1](../core/memory-management.md)（Frozen）。

| 写法 | 常见语义（须由接口文档确认） |
| --- | --- |
| `T` | 按值持有该类型对象；是否拥有其引用的资源由类型定义，复制视图不产生所有权 |
| `T&` / `const T&` | 非空借用 / 经该引用只读的借用；均不自动保证对象存活或线程安全 |
| `T*` / `const T*` | 可空借用（约定，不由语言强制） |
| `std::unique_ptr<T>` | 排他所有权 |
| `std::shared_ptr<T>` / `std::weak_ptr<T>` | 明确需要时的共享所有权 / 弱观察 |
| `std::span<T>` / `std::string_view` | 连续数据或字符串的非拥有视图 |
| `Handle<T>` | 资源标识/引用；拥有型或非拥有型由 Registry 契约定义 |

- **MUST** 明确分配器来源、对象销毁者、Borrow 失效条件及跨线程的访问保护；RAII 优先。
- **SHOULD** 按存储寿命、访问模式和测量结果选择 SystemAllocator、Frame Linear/Arena、Pool/Slab；不强制每次分配做运行时查表或附标签。
- **MUST NOT** 把 Arena 重置当作对象析构；在 CPU Task、GPU 访问和借用结束前不得复用存储。
- **SHOULD** 对可流送或可热重载的 Entity/Asset 使用强类型 Handle/Registry 访问；GPU 资源遵循已冻结的 **Handle + Deferred Destruction** 契约。解析出的指针依旧是短期借用，Handle 的拥有与存活保护语义不能凭名字推断。
- **SHOULD NOT** 默认使用 `shared_ptr` 解决所有 Lifetime 问题；也不一刀切禁止其在确有共享所有权时使用。
- 无异常环境中原生 Try 分配与 PMR 的失败策略不同：PMR 失败即终止，不假定 STL 容器扩容可恢复 OOM。
- 资源释放与 GPU Deferred Destruction、跨 Module 析构及 CodePin 的顺序以 Memory / Module 专题规范为准。

## 7. Exception-Free 与错误处理

**权威约束：**[错误处理与基础诊断](error-diagnostics.md)（Frozen 的是语义，不是 API）。

- 可恢复失败：无堆分配结构化 Error 值 + `std::expected` / Result；调用方显式检查、处理或传播。
- 正常缺失：普通结果或 `std::optional`；程序员违反内部不变量：Assert 或始终有效的 Check；不能安全继续：Fatal。
- **MUST** 验证外部输入：网络、存档、资源文件、Plugin/Mod/IPC 等不能只依赖 Debug Assert。
- **MUST NOT** 让可选的 Assert、Trace 或日志的开关改变业务正确性。
- 禁用异常下 `std::expected::value()` 的失败路径不适合作为正常错误处理；检查后再使用结果，且以所选 STL 实现验证无异常行为。
- `Result` 自身不保证 OOM 可恢复；如需支持，整条调用链必须使用明确的 Try 分配及回滚策略。

不冻结新 API 名称的示意：

```cpp
// 位于 pg 命名空间；Error、Path 等类型定义省略，仅示意公共接口的错误契约。
[[nodiscard]] std::expected<TextureId, Error> LoadTexture(Path path);

auto result = LoadTexture(path);
if (!result)
{
    return std::unexpected(result.error());
}
UseTexture(*result);
```

## 8. ECS、Data-Oriented 与 Event-Driven

**确定事项：**Runtime ECS 选用 **flecs**；World / Meta / Scheduler 的详细接口尚未冻结，见 [Runtime](../runtime/README.md)。

- **SHOULD** 将 Entity 视为身份、Component 作为数据及不变量、System 作为行为；避免 Component 承载庞大对象继承树。
- **SHOULD** 依据访问模式选择 AoS / SoA、批处理和缓存友好的布局；不是所有 Component 都必须 SoA。
- Data-Oriented 关注数据布局和访问模式；Data-Driven 关注用配置、资源或规则数据表达行为。两者可结合，也可分别采用。
- **SHOULD** 对静态已知的 Component 集合采用强类型 Query，以保留类型检查。性能评估须区分查询构建/字符串解析、缓存维护和迭代成本；避免在热循环反复构建相同查询，不能仅凭构建时使用字符串就判定后续迭代较慢，详见 [flecs Queries](https://www.flecs.dev/flecs/md_docs_2Queries.html)。
- **SHOULD** 使 System 的 Read/Write 依赖可分析，为后续任务 DAG / 并行调度提供依据；还须覆盖外部资源、共享状态及 ECS 结构变更，不能仅凭 Component 读写声明推定可安全并行。访问注解、调度策略、系统执行顺序均尚未冻结。
- **SHOULD** 区分 Event（已发生事实）与 Command（执行请求）；Message 可作为通信载荷的泛称，具体分类由领域规范定义，不要求三者互斥。以类型安全载荷为主，订阅、路由、排队和执行阶段由后续消息系统定义。
- 事件**不是 ECS 状态的替代品**；不能从“Event-Driven”推导所有交互都必须走全局 Event Bus，也不能假定 Event 一定同步或一定异步。
- 动态订阅、回调和 Registry Contribution **MUST** 遵守 Module 卸载前的撤销、在途任务 Drain 与代码驻留条件。

## 9. Determinism、Scheduling 与 Thread Safety

- **SHOULD** 在需要重现的 Gameplay、Replay 和测试中明确时间推进策略、随机源及其消费顺序、输入序列、System 顺序与并行同步边界。固定步长是常用方案，但它与固定随机种子都不足以单独保证确定性。
- **MUST NOT** 依赖 `unordered_map` 迭代顺序、指针地址或操作系统线程交错作为 Gameplay 的业务顺序协议。
- 需要跨 CPU 架构逐位确定性时，浮点选项、SIMD、FPU、数学库、物理实现与线程排序必须**额外**设计与验证；“Deterministic First”本身不是跨平台位级一致性的承诺。
- **SHOULD** 由引擎统一 Task/Job Scheduler 负责并行依赖、取消和收尾；Gameplay 避免裸 `std::thread(...).detach()`。
- **MUST** 保证异步任务访问的对象、借用视图背后的存储及回调实现代码在整个访问期间有效；仅复制 Span 或捕获裸指针不能提供这一保证。ServiceRef / Borrow / Lease 的存活保护遵循 [Service](../core/service.md)，并须声明 API 的 Main/Render/Worker Thread 约束。
- 数据默认只读，修改权限显式表达；`const`、只读视图和 `shared_ptr` 的引用计数均不保证被引用对象可安全并发访问，共享可变状态仍需同步契约。
- **SHOULD** 使用 RAII 管理锁，并明确锁顺序；不要持有内部互斥锁调用行为未知的 Plugin/用户回调。释放锁后调用时，仍须保留实例与代码存活保护；条件变量等待须围绕受同步保护的谓词处理唤醒。
- **SHOULD** 优先显式状态机（如 `enum class` / `variant`）而不是多个互相矛盾的布尔标志。

## 10. Meta、Schema、Platform 与 Module 边界

- `pga` 属性、Clang AST 提取及代码生成的边界、Schema 复用方向统一以 [Reflection / Meta](../core/reflection-meta.md) 为准；**Taxonomy 与生成协议尚未冻结**。
- **MUST NOT** 用 `PG_CLASS` / `PG_PROPERTY` 标记替代 `pga`；`[[pga::...]]` 只是待验证的语法方向，不应宣称原版 Clang 已原生支持全部自定义语义。
- **SHOULD** 避免 Gameplay 热路径重复执行可以预生成的反射查找、Schema 解析或资源烘焙；需要动态 introspection 的 Editor、Console、Plugin 可明确使用 Runtime Meta。
- **MUST** 区分序列化格式与 `sizeof(C++ Struct)` 内存布局；持久化协议需独立声明版本、编码、字节序、字段演化。
- 平台 API **SHOULD** 将 OS/SDK 专属实现隔离，不让大量 `#if` 或运行时平台分支散落在业务层；目标平台已确定时优先编译期选择，设备动态能力检测仍是 Runtime。
- **MUST** 遵循 CMake 构建单 Module 单库、不新增 BinaryImage 层、Concrete/Contract/Registry Service 三模式及 Module CodePin 安全卸载规则，见 [构建系统](build-system.md)、[Plugin / Module](../core/plugin-module.md)、[Service](../core/service.md)。
- Pangu 内可控、ABI 配置兼容的 C++ Module **允许传递 STL**；`std::string`、`vector`、`shared_ptr`、`function` 等仍需约定分配域与销毁代码有效期。**不应**将“ABI 一致”误解为可以随意跨 Module free 或提前卸载 Provider。
- `AbiProfile` / `AbiFingerprint` 的 Schema、序列化、算法与严格兼容策略已由 [ABI Profile / Fingerprint v1](abi-profile-fingerprint.md) 冻结；C++ API、运行时 Manifest 及实际工具链接入仍未冻结，见 [ABI](abi.md)。

## 11. 工程质量与代码风格

- 按仓库 `.clang-format` / `.clang-tidy` 配置执行；新增检查规则应经项目验证。第三方代码与引擎本体的 Warning Policy 分离。
- **MUST**：PanguEngine 自有 C++ 头文件统一使用 `.h` 扩展名，不使用 `.hpp`；已有 ThirdParty 源码不因此批量改名。当前 Module 公开头文件直接位于对应 `Public/` 下（如 `Source/Runtime/Module/Public/ModuleIdentity.h`），私有头文件位于 `Private/`，引入时使用 `#include <ModuleIdentity.h>`。
- **SHOULD** Include What You Use；PCH 用于编译性能，不掩盖实际头文件依赖。C++ Modules 语言特性的使用范围另行决策。
- **SHOULD** 让 Core System 可在不启动完整 EngineHost 的测试环境独立运行；通过显式 Host/Context/World 依赖避免隐藏全局单例。
- **SHOULD** 把 Profiler/Trace、内存统计、日志和任务跟踪设计为可裁剪的诊断功能；Shipping 可以移除可选成本，但不能移除安全访问、正确释放或必要同步。
- **SHOULD** 使用固定输入、固定随机种子和可重放条件做系统回归；使用 Clang warnings、Sanitizer（ASan/UBSan/TSan 在实际目标支持时）、Benchmark 与多平台验证。
- 临时验证探针使用 `.probe/`；长期维护的回归进入 `Tests/`。**不要把 PoC、设计示例或个别平台可编译当作完整实现验收。**

### 当前风格配置

| 项目 | 当前设置 |
| --- | --- |
| `clang-format` 基础 | LLVM |
| 语言标准 | C++23 |
| 缩进 | 4 空格，禁用 Tab |
| 行宽 | 120 |
| 大括号 | Allman |
| 指针星号 | 类型侧：`int* value` |
| 类型名 / 函数名 | CamelCase / CamelCase |
| 数据成员 | CamelCase（无前后缀） |
| 枚举值 | `kCamelCase` |
| 普通变量 / 命名空间 | camelBack / lower_case |
| 常量 | `kCamelCase`（详见 clang-tidy） |

`.clang-tidy` 当前使用多组 bugprone、clang-analyzer、modernize、performance、portability、readability 和 cppcoreguidelines 检查；`WarningsAsErrors` 当前为空。不可把尚未接入的 CI 或“所有 Warning 作为 Error”写成已实施事实。

通用 C++ 实践可参照 [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) 的 F.16、C.20/C.21、ES.20、CP.22/CP.42 与 Per.6；按本项目的无异常及领域契约选用，不整体照搬其异常、命名或库选型建议。

## 12. 新 API / System 评审清单

1. 可以在 Compile-Time 或 Build-Time 安全确定吗？实际需要哪些 Runtime 动态性？
2. 是否显式表达类型、错误语义、Ownership、Borrow 和 Lifetime？
3. 抽象是否引入额外 Heap / Lock / Dispatch / Lookup / IO / GPU Sync？是否必要且有测量证据？
4. 数据布局和接口是否匹配 ECS / 渲染 / 任务的真实访问模式？
5. 是否约定线程模型、调度依赖、取消、回滚及 Module 卸载 Drain？
6. 是否将安全性和正确性依赖于可关闭的 Assert、日志或 Profile？
7. 跨 Module 的 ABI、Allocator、STL 所有权及 CodePin 约束是否满足？
8. 五平台中的特定行为、外部输入校验及版本化格式是否明确？
9. 是否可独立测试、Profile 和诊断？文档状态是否与实际实现相符？

## 13. 尚未冻结（不得由本文件推定）

`Result/Error` 的具体 C++ 签名和布局、ABI Profile/Fingerprint 的实际采集工具与运行时接线、RTTI 全局编译策略、C++ Modules 的采用范围、Module 的 Public/Internal/Private 目录细节、事件与 Delegate API、ECS Query/Read/Write 标注、Scheduler 依赖推导与线程调度 API、序列化格式、统一 Schema / `pga` Taxonomy、Platform HAL、Callback 借用包装类型等仍待各专题文档冻结。错误与基础诊断、内存所有权、Module 单库和 Service 三模式的**现有已冻结语义不因此重新开放**。
