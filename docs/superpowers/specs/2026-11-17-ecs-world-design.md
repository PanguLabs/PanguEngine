# ECS 首版：flecs 原生接口与 World 生命周期

> 状态：首版设计契约已确认并实施。公开 C++ API/ABI 尚未冻结。
> 正文保留实施前的设计背景。当前实现和平台覆盖见[验收计划](../plans/2026-11-18-ecs-world.md#ecs-交付验收)。
> 日期：2026-11-17
> 最近更新：2026-11-18
> 设计起点：`d71a632`。
> 用户已确认：建立 ECS Module，业务保留 flecs 原生类型与强类型 API，接受依赖升级后的统一重编；首版 World 存活期间保护提供方代码，World 关闭后再允许卸载提供方。
> flecs 参考基线：用户指定 `v4.2.0`，对应提交 `6566d4b7bdc597d0b74d5e0a4104d4ae6ac49402`。
> flecs 接入方式：运行库按上游方式用 C 编译，ECS 和业务通过 `<flecs.h>` 使用原生 C++ API。
> 关闭方案修订：按用户要求结合 UE/O3DE 对照，采用宿主统一保持模块代码有效、先排空 World 再推进 Module 关闭的方向，取代逐 World Pin 与可声明预清理能力方案；具体接口仍未冻结。
> 命名空间方案：按用户最新要求保留 `pg::flecs` 与 `pg::ecs`；flecs 原生类型通过 `pg::flecs::` 访问，不直接引入 `pg` 根命名空间，见第 4.1 节。

## 1. 目的与现状

让现有 EngineHost、Module 和 Scope 管理第一个实际 Runtime 功能：创建相互隔离的 ECS World，执行组件操作与查询，最后在所有对象及回调代码仍有效时安全关闭。

当前 Runtime 只有 Foundation、Log、Module、EngineHost。EngineHost 已支持 Context/World Scope、Concrete 具名访问、ScopeAccess/ScopeBorrow 和可重试关闭；这些 World Scope 目前不自动包含 flecs World。flecs 尚未作为正式 ThirdParty 接入。用户指定参考基线为 [flecs v4.2.0](https://github.com/SanderMertens/flecs/releases/tag/v4.2.0)。本地 `../flecs` 已由用户检出该版本，已核对当前 HEAD 与 tag 均指向 `6566d4b7bdc597d0b74d5e0a4104d4ae6ac49402`，版本宏为 4.2.0；后续实现细节使用此版本源码核对，正式接入仍固定同一提交。

既有依据：[ThirdParty](../../../wiki/foundation/third-party.md)、[Service](../../../wiki/core/service.md)、[EngineHost](../../../wiki/core/engine-host.md)、[Module API](../../../wiki/core/module-api.md)、[内存所有权](../../../wiki/core/memory-management.md)、[C++ 规范](../../../wiki/foundation/cpp-guidelines.md)。本设计不把未交付能力写成已实现状态。

## 2. 已选方案与替代方案

| 方案 | 收益 | 代价与结论 |
| --- | --- | --- |
| 各业务目标直接依赖 flecs，不建立 ECS Module | 最短接入路径，直接使用上游能力 | World 所有权、配置、代码保护与关闭责任容易分散；未选择 |
| ECS Module 管理引擎契约，业务保留原生 flecs API | 集中管理依赖、World 生命周期和代码保护，保留原生强类型查询 | 公开接口接受 flecs 类型和版本耦合；用户已选择 |
| 完整封装独立 Entity/Component/Query/System API | 引擎可以控制全部表面接口 | 需要持续映射上游能力；当前没有第二后端需求，不作为首版路线 |

本设计中的 Pangu Module 是代码、构建及生命周期单元。flecs 自身用于组织组件和系统的 module/import 机制不代替 Pangu Module，也不提供 Pangu CodePin 或 ScopeBorrow。

## 3. 首版范围

### 3.1 交付能力

- 一个真实 `PanguECS` Runtime Module，别名 `Pangu::ECS`；公共命名空间 `pg::ecs`。
- 一个固定上游提交的普通 `ThirdParty::Flecs` 目标；消费者通过 ECS 的公共依赖获得一致的头文件和配置。
- 一个由 ECS Module 提供的公共命名空间入口，flecs 原生类型通过 `pg::flecs::entity` 等写法访问，保持原生类型身份。
- ECS 在启用它的宿主中声明 World 级 Concrete 实例：每次成功创建 World Scope，恰有一个独立 flecs World；Host/Context Scope 不额外创建 ECS World。
- 具名入口接收 EngineHost 和精确 World Scope，返回现有 ScopeAccess；实际访问必须由 ScopeBorrow 保护。
- 多 Module 可在同一 World 内显式登记组件、System、Observer；业务保留原生实体操作、组件读写与 Typed Query。
- 应用在主线程串行推进 ECS，显式提供时间步长。由 EngineHost 管理 World 的创建与关闭，应用负责主循环。
- Scope 单独关闭、Context 级联关闭、整体宿主关闭及装配失败回滚，均可安全销毁 ECS World。
- 三桌面双模式运行验证，以及 Android/iOS ARM64 两模式编译和最终链接验证。

### 3.2 后续能力与反目标

首版支持实体、组件、关系、Typed Query、System、Observer 和显式推进。按这些能力选择 flecs addons。HTTP/REST、脚本和其他可选能力留待各自的集成需求。保留 World 的 Gameplay 热卸载/Hot Reload、持久化实体身份、序列化、Editor Inspector、PanguMeta 到 flecs 的自动映射、自有 TaskScheduler 集成及 GPU 排空均为后续专题。

避免逐方法转发的通用 ECS 包装、第二后端接口、进程级默认 World、第二套 Scope/加载器、每个组件单独借用/Pin、热循环中的动态 Service 查找，以及将普通实体 ID 当作持久化 ID。

## 4. 构建、依赖与实例所有权

`Source/Runtime/ECS/` 对应唯一实际 STATIC/SHARED Module Target，服从统一 Monolithic/Modular 模式。ECS 的具名访问桥接消费 EngineHost，依赖方向为 ECS → EngineHost。

需要 ECS 的应用显式链接 `Pangu::ECS`，在两种模式下都通过 `HostStartupConfig::AdditionalResidentQueries` 接纳 ECS。应用的链接持有覆盖宿主关闭及析构。ECS 在业务候选装配前激活。Monolithic 业务 Catalog 仅列出业务候选，Modular 业务 Module 通过 `SharedModuleCandidate` 接纳，ECS 在每个启用它的宿主中只接纳一次。其常驻范围由应用选择，现有 Builtin 常驻根仍适用于所有宿主。

ECS 的 Module Query 和应用的业务 Host Catalog 沿用 PanguMeta 构建接线，按链接模式生成和消费所需代码。普通 C++ 组件由业务代码显式登记到 flecs，组件需要 Pangu 反射时再使用 `pga` 元数据及既有 PanguMeta 流程。

`Source/ThirdParty/Flecs/` 保存项目接线与来源说明，`Repo/` 使用固定提交 submodule。首版以用户指定的 `v4.2.0` 为接入基线，固定到提交 `6566d4b7bdc597d0b74d5e0a4104d4ae6ac49402`，接入前核对许可证和所需功能；不把浮动 master、邻接目录或配置期下载作为构建输入。版本不再留给实施者另选，升级须单独说明。

v4.2.0 将多 World 的 C++ 组件 ID 支持划入 `FLECS_MULTI_WORLD` addon；首版多 World 契约要求保留它。构建裁剪还须保留系统执行、progress 和缓存查询所需的 addons，不把缺失功能的精简配置当作本设计已经验收。

Monolithic 使用普通 STATIC flecs。Modular 中的应用、ECS 和业务 DLL 链接并部署同一份 SHARED flecs。`ThirdParty::Flecs` 是普通 CMake 目标。同一 World 的创建、操作及销毁均通过这份 runtime。公共依赖、导入/导出、原生动态依赖及部署规则须在真实消费测试中验证。

flecs 运行库按上游方式以 C 编译固定版本的实现源码。`ThirdParty::Flecs` 公开传递 `FLECS_CPP`，Pangu 自有适配与业务使用 C++23，通过 `<flecs.h>` 提供的原生 C++ API 访问 `pg::flecs::world`（即 `::flecs::world`）和强类型查询。平台工具链、C runtime、PIC 和系统库按实际 Target 接线，第三方采用独立的警告策略。首次接入需核对根构建图的 C 语言支持。

World Scope 是实例 Owner，ECS Module 是实现代码 Owner。业务访问对象可以是带少量引擎状态的 `pg::ecs::World`，内部拥有 flecs World；它不可复制、不可移动，不另建通用 Service 基类。具体类型名、签名、ModuleId 和 DeclarationId 由实施计划确定，不在本稿冻结。

具名入口以 EngineHost 和精确 World Scope 获取 `ScopeAccess`。成功 `ScopeBorrow` 后可取得 `pg::flecs::world&`，用原生 API 操作实体、组件和查询。借用约束是调用契约，公开 flecs 类型本身无法在编译期阻止对象逃逸。禁止通过借来的 World 引用重置、释放或接管宿主拥有的 World。

- `pg::flecs::entity` 值可以保存。访问实体时须重新确保对应的原 World 仍有效且受 Borrow 保护。World 退休后原实体值失效，即使另一 World 存在相同数值的 ID。
- 原生 World 的副本、由 World 指针构造的拥有型对象、`entity.world()` 返回的拥有型对象，都须在对应 Borrow 释放前销毁。Iterator、组件引用和裸指针的使用也限定在有效借用以及上游允许的寿命内。
- Query 的所有者负责先销毁 Query，再释放对应 World Borrow。外部缓存可一并持有二者，进入 Closing 后应在等待关闭完成前主动释放，否则宿主报告 Busy。World 内部缓存先于原生 World 销毁，通过 World 自身所有权保证有效期。首版关闭由这些所有者协作推进。

### 4.1 命名空间入口与访问边界

保留上游 `::flecs` 命名空间，在 Pangu 自有公共头中将 `pg::flecs` 声明为它的命名空间别名。Pangu 业务代码通过 `pg::flecs::` 访问 flecs 原生类型和 API；`pg::ecs` 保留 Pangu 自有集成接口。此方案不修改第三方源码、不重命名上游符号，也不通过宏替换 `flecs` 标识符。

公共头只提供命名空间别名，不在 `pg` 根命名空间逐项引入类型。示意如下，最终公共头文件名由实施计划确定：

```cpp
#pragma once

#include <flecs.h> // IWYU pragma: export

namespace pg
{
namespace flecs = ::flecs;
} // namespace pg
```

| Pangu 入口 | 对应的原生声明 | 语义 |
| --- | --- | --- |
| `pg::flecs` | `::flecs` | 命名空间别名，访问当前启用的原生 API |
| `pg::flecs::entity` | `::flecs::entity` | 同一原生实体类型 |
| `pg::flecs::iter` | `::flecs::iter` | 同一原生迭代器类型 |
| `pg::flecs::query<T...>` | `::flecs::query<T...>` | 同一原生查询模板及其实例 |
| `pg::flecs::world` | `::flecs::world` | 同一原生 World 类型，保留其原有拥有语义 |
| `pg::ecs::World` | Pangu 自有具体类型，名称尚未冻结 | 承担第 4 节的宿主集成和所有权职责，与 `pg::flecs::world` 区分 |

别名下的类型声明仍属于 `::flecs`，没有新类型、继承或转发对象，不改变类型身份、模板特化所属命名空间或原有生命周期，也不增加运行时操作。别名不会形成独立的 Pangu ABI，升级仍按固定 flecs 版本统一重编。Pangu 侧代码与示例统一使用 `pg::flecs::`；上游源码和文档仍保留其原有 `flecs::` 拼写。

公共头自包含，并将 flecs 头标记为显式导出入口，使消费者仅包含该公共头即可使用 `pg::flecs::` 访问原生 API。正式实现时还需用实际目标的编译数据库核对静态 include 检查中的头文件提供关系，不能要求消费者逐个包含上游实现头。

`pg::ecs` 保留为可扩展的真实 Pangu 命名空间，用于集成接口；不能将它整体设为 flecs 别名后再加入自有 World。`pg::log` 继续表示 Pangu 日志，flecs 日志从 `pg::flecs::log` 访问。公共头不使用 `using namespace ::flecs`，也不通过 `using` 声明或类型别名把 flecs 类型直接放入 `pg` 根命名空间；原生类型、函数、常量和嵌套命名空间统一留在 `pg::flecs` 入口下。

别名只是语法入口，不授予新的访问或拥有权。`pg::flecs::world` 的构造、复制及原生访问仍遵守本节前述约束；业务访问宿主管理的 World 时继续使用其具名入口和 ScopeBorrow，不能用别名绕过第 6 节关闭契约。

## 5. 登记与提供方代码保护

### 5.1 显式登记

Module 注册阶段声明可重复执行的 World 工厂及安装资源。同一宿主的全部 World 安装同一组 Active Module 的 World 声明，差异由各 World 的数据和系统启用状态表达。World Profile、逐 World Provider 筛选和构造配置属于后续需求。

每个 World 装配时，ECS 先创建 World。业务安装资源通过显式资源依赖取得它，沿现有依赖顺序在 Factory/Start 阶段完成组件、System、Observer 的登记。`CreateWorld` 成功返回后，应用才创建首批业务实体并驱动系统。登记失败沿用 Scope 回滚流程。

统一启用 `FLECS_CPP_NO_AUTO_REGISTRATION`。业务在首次使用 Typed API 之前显式登记顶层 C++ 组件类型，标签、关系及其所需类型遵守同一规则。显式登记允许 flecs 自动派生枚举常量等内部实体。该选项的检查范围是 C++ 隐式类型登记。

跨模块共享组件由一个定义方首次登记。定义方负责组件类型、首次登记的名称及生命周期 Hooks。使用该类型的每个 DLL 或可执行文件，须为其访问的每个 World 建立本地 Typed 类型绑定。消费方使用同一份 C++ 类型定义，在定义方登记之后按名称或类型符号复用既有组件，保持原有 Hooks。正式接线使用这条复用路径，避免通过显式组件 ID 重新安装 Hooks。首次绑定仍可能运行 flecs 的枚举派生登记，须验证既有类型和派生状态。组件 ID 的一致性按同一个 World 判断，允许不同 World 的数值不同。

### 5.2 宿主代码持有与登记约束

首版由 EngineHost/ModuleManager 统一保持本宿主已接纳的 Module 代码有效。业务集合 Ready 后固定，World 存活及清理期间不对其提供方执行独立卸载。World 不必为每个提供方另存一张 CodePin，也不新增“取得当前工厂所属 Module Pin”的 ScopeBuildContext 接口。

向 World 登记非平凡组件生命周期函数、System/Observer 回调、捕获对象的析构或释放器之前，提供方及其必要代码依赖必须已经属于本宿主受管理的 Module 集合。Application 自有入口等宿主外部代码，由其拥有者明确保证有效期覆盖全部 World 清理。不能只按组件名字推断代码归属，也不能将未受管理的动态库入口直接留给 World。

代码保持一直覆盖原生 World 销毁及其全部回调返回。当前实体数为零、System 被禁用或组件实例清空，都不能作为提前卸载提供方的依据；整体关闭顺序见第 6 节。业务访问桥接不暴露 ModuleManager，不能绕过宿主生命周期自行卸载提供方。

登记开始后失败，已经写入 flecs 的状态仍可能包含代码入口，必须随失败 World 一起清理。宿主继续保持提供方代码和清理所需的父 Scope 实例；不能因为安装资源失败返回或析构，就在失败 World 尚未销毁时开始原生卸载。

回调数据由 World 持有，或由登记方显式依赖所属 Context/Host 的资源，并通过 Scope 关闭顺序保证该父实例活到 World 清理结束。安装资源可以先退出，回调不得借用它的 `this` 或更早销毁的状态。清理沿用已建立的所有权和依赖，普通 Borrow 在 Closing 后按第 6 节关闭接纳。Module 依赖保持 Gameplay → ECS 的方向。

首版不增加 Ready 后的 Module 集合变化。若在运行阶段动态创建 System/Observer，它保存的全部代码和数据仍须满足上述已建立的保护关系，不允许借原生 API 绕过登记契约。

## 6. 宿主关闭顺序与代码驻留

### 6.1 采用宿主统一持有与有序清理

依据 [UE/O3DE 源码对照](../../research/2026-11-17-ue-o3de-module-lifetime.md)，两者均通过较长生命周期的模块所有者、代码驻留及明确清理顺序保护对象和回调；不能由此推导每个 World 都必须额外持有 CodePin。UE 正常退出保留 DLL 到进程结束，O3DE 正常 Gem 加载保持模块共享引用并先清理使用者；两者的具体限制和版本边界见对照记录。

Pangu 首版采用宿主统一持有方案：本宿主的 World、组件、System、Observer 及其清理入口全部完成使用之后，才进入 Module 关闭和原生卸载。`Gameplay → ECS` 继续表示功能和 Module 依赖；World 保存 Gameplay 回调所要求的代码有效期，由宿主的生命周期顺序保证，不在 Module 依赖图中增加 `ECS → Gameplay` 反向边。

本稿据此撤回“World 为每个提供方保存 Pin”和“工厂声明 World 预卸载清理能力”的首版要求。首版通过统一关闭阶段满足已经确认的保守卸载契约，不增加 ECS 专用卸载分支或另一套资源依赖图。

### 6.2 整体关闭阶段

EngineHost 仍使用现有公开 Cold/Starting/Ready/Closing/Closed 状态；Closing 内部按以下阶段串行推进，BeginShutdown/TryFinishShutdown 保留现有重试形式：

1. **关闭业务接纳。** 宿主进入 Closing，关闭 Module、Scope 和资源的业务接纳闸门。拒绝新 Scope、新借用和新业务代码 Pin；已经发出的 ScopeAccess 也不能再借入。已存在的 Borrow 及其代码保护继续有效，必要的 Stop/Poll/Destroy 清理入口继续可调用。此动作不开始逐 Module 卸载。
2. **排空本宿主全部 World Scope。** 宿主沿现有 Scope 所有权记录找到仍存活的 World，包括创建失败但清理尚未完成、未交付给应用的 Scope，使用现有资源依赖和 Scope 清理器停止并销毁其资源。此时保持本宿主全部 Module 的原生持有，Context/Host Provider 实例也保持有效；不执行这些父 Scope 资源的停止、销毁或 Module 的原生释放。
3. **确认 World 清理完成。** 每个原生 flecs World 已完成实际销毁，组件析构、移除 Hook、Observer 和绑定上下文清理、World 内部 Query 释放均已返回，相关 Scope 已退休。安装资源先退出不代表 World 已销毁；回调所需的数据不能借用已经销毁的安装资源。所有 World 完成前，整体关闭不能进入下一阶段。
4. **推进既有 Module 关闭。** 再启动现有 Module 关闭与依赖清理流程，按其规则处理 Context/Host 资源、声明和 Module 对象，等待外部 CodePin，并执行原生释放。失败候选批次和原生释放失败时的 Provider 保留规则继续适用。Module 管理完成后，宿主再按现有顺序停止 Log 并进入 Closed。

任何 World 的外部 Borrow、业务执行或 Stop/Poll 仍未结束，宿主都保留在 World 排空阶段，保持必要实例及全部 Module 代码有效，报告 Busy/具体错误并允许重试。不能通过跳过 World、提前卸载 DLL、超时强毁或丢失失败 Scope 来继续下一阶段。

这里要求的是实际代码仍然驻留：复制 ModuleDescriptor、保留函数地址或单纯保存 ModuleHandle 都不构成持有证明。复用当前 ModuleManager 的原生持有和宿主所有权；没有必要为同一保证再叠加一组 World 内部 Pin。存在宿主之外的代码引用时，仍使用其明确的所有权或现有 CodePin。

### 6.3 当前实现缺口与职责边界

当前 `EngineHost::BeginShutdown` 在进入 Closing 后直接调用 `ModuleManager::BeginShutdown`；State::FinishShutdown 也直接推进 Manager 关闭。Manager 清理则以 Module 为单位处理资源和原生持有。因此上述宿主阶段尚未实现，不能只把公开状态改成 Closing 就声称新借用已经全部被拒绝，或认为现有关闭路径自然包含了 World 排空。

实施时由 EngineHost 统一协调关闭阶段；Module/Scope 层仅补齐其所需的接纳关闭、World 枚举/排空和完成判定能力，复用现有 Scope 记录、资源 DAG 与清理器。接纳关闭必须与已有借用令牌共享有效的关闭闸门，但不能过早把全部 Module 送入逐项卸载，从而重新让 World 清理受 Module 卸载先后顺序阻塞。具体内部入口和状态表达留给实施计划，不预先增加通用插件扩展点或 ECS ModuleId 特判。

宿主负责清理全部受其管理的 World，不要求 Application 在调用 BeginShutdown 前手动枚举并关闭。显式关闭、启动失败兜底、析构有限同步兜底和失败 Scope 的清理责任，均须汇入一致的宿主关闭阶段；不存在 World 时直接进入既有 Module 关闭。

这项修订明确增加 EngineHost 的 World 排空责任，不替换独立使用 ModuleManager 的 BeginUnload/Shutdown 契约，也不削弱现有 Provider 与原生失败屏障。实施时同步 EngineHost、Service 及必要的 Module/Scope 规范，并用针对性测试确认两条路径各自的行为。

### 6.4 单独关闭、失败回滚与多个宿主

单独关闭 World 或 Context 时，宿主仍保持 Ready，业务 Module 集合及其代码持有保持不变。沿用 Scope 关闭机制：关闭相应 Scope 的闸门 → 排空访问/执行 → 按依赖销毁 World 内资源及原生 World → 完成 Scope 退休；不借这个动作卸载提供方 Module。一个 World 关闭不影响其他 World 的数据和查询。

创建失败只回滚失败 Scope，不污染已经 Ready 的其他 World；未交付 Scope 的清理残留仍归现有 PollCleanup/宿主所有者负责。若错误升级为整体关闭，也必须先排空全部残留 World，再推进 Module 关闭；不能在错误路径中绕过阶段约束。

同进程多个宿主各自持有其 Module 接纳和 World，关闭只针对本宿主推进。不能用进程退出代替清理，不能用进程全局 flecs reset 清除别的宿主状态；一个宿主释放其原生持有，不表示其他宿主仍在使用的同一动态库已经物理卸载。

### 6.5 CodePin 的定位与后续能力

现有 CodePin 继续保护显式的外部代码引用。World 排空不等待无关外部 CodePin 归零；World 销毁后，外部 Pin 仍可让 Module 原生卸载返回 Busy，调用方释放后再推进。Pin 不保护实例，也不能授权调用已经停止的 Service。

首版不实现保留 World 的独立 Gameplay 卸载。如果将来开放这种能力，需要重新设计提供方登记撤销、组件和回调清理、外部引用失效及必要的逐提供方代码持有；不能只复用本节的整体关闭顺序就宣称热卸载安全。

## 7. 执行、线程和错误边界

首版在应用主线程串行创建、登记、访问、推进和销毁所有 World，flecs 内部工作线程保持关闭。`ScopeBorrow` 的跨线程释放沿用现有契约，ECS 数据访问仍在主线程。

应用持有 World Borrow 并通过 `pg::ecs` 的推进入口提供以秒计的有限正步长。非法步长按引擎 `Result` 契约返回错误，合法步长调用一次 flecs `progress()`。应用只推进 Ready 的 World，暂停或关闭时跳过推进，单步时传入明确的正步长。上游 `progress(0)` 会使用自动采样时钟，引擎推进入口始终使用应用给出的步长。

`quit()` 的结果在本次 `progress()` 返回后交给应用。应用停止继续驱动该 World，并决定何时关闭它、其他 World 或整个 Host。迭代与回调期间禁止重入 Scope/Host 关闭。World 的 `RequestStop` 只发出停止意图，现有 Borrow 排空后才销毁 World。

跨多个 World 使用同一个 C++ 组件类型时，需核对 flecs 类型缓存和组件 ID 的跨 World/跨 DLL 行为；不把原生组件/实体 ID 当作全局稳定标识，不在活跃的多宿主环境使用进程级 reset 修复 ID 冲突。

ECS 自有接入错误使用现有 Result/FailureContext 机制。所有配置统一启用 `FLECS_KEEP_ASSERT`，保留 flecs 的普通断言与检查，并使用上游的终止语义。仅调试配置生效的上游检查仍按原有条件编译。验收单独记录保留检查的性能成本。

首版不安装指向某个 EngineHost/World 的进程全局 flecs OS/log 回调。同进程多个宿主不得互相覆盖第三方全局配置；确需平台初始化时，由稳定驻留实现一次性处理，不能在可卸载业务 DLL 中留下全局函数指针。

## 8. 验收要求

正式 ECS 测试进入 `Tests/ECS/`，宿主关闭阶段测试归 `Tests/EngineHost/`，必要的 Module/Scope 支持行为测试归 `Tests/Module/`；Project/部署验证复用 `Tests/BuildSystem/`。遵守 [Tests 维护规范](../../../Tests/README.md)，不复制整套上游正确性测试，不依赖未版本化 Probe，不因本切片完成删除长期测试。

端到端验收使用正式的无窗口 Project 夹具。应用显式接纳常驻 ECS，Gameplay Module 登记组件和 System。应用在创建 World 成功后使用原生 API 装配实体，通过引擎入口推进并断言结果，最后推进宿主关闭。该主线覆盖 Monolithic 和 Modular，另以定向用例覆盖借用、失败路径及真实原生卸载。

| 场景 | 必须证明的行为 |
| --- | --- |
| 最小 ECS 使用 | World Ready 后创建首批实体。普通 C++ 平凡/非平凡组件、强类型查询、System 和 Observer 得到可断言的数据变化 |
| 显式推进 | 有限正步长执行一次，零、负数和非有限值返回错误。应用跳过推进时保持暂停。单个 World 的 quit 结果由应用处理，其他 World 可继续推进 |
| Release 检查 | 遗漏显式登记触发保留的上游断言，使用隔离进程验证终止并单独记录检查开销 |
| 跨模块登记 | 在真实 DLL 中验证定义方先登记、消费方再绑定。同一 World 的组件 ID 一致，生命周期 Hooks 仍调用定义方，枚举派生状态符合上游行为 |
| 命名空间访问边界 | 公共头自包含；`pg::flecs` 下类型与上游类型及查询模板实例身份一致，与 `pg::ecs`、`pg::log` 共存；原生 World 与宿主 World 保持区分，公共头不暴露 `pg` 根下的 flecs 类型入口 |
| 多 Context/World/Host | 同一宿主的 World 安装同一组声明，各 World 的数据、组件绑定和关闭相互隔离。验证不同 World 创建顺序和无关类型登记顺序 |
| 精确 Scope 与访问 | 错误 Host/Context/World、过期句柄及关闭后访问拒绝；既有 Borrow 阻止销毁，释放后能完成关闭 |
| 正常 World 关闭 | 原生 World 实际销毁时组件及绑定上下文正确清理，提供方代码仍有效 |
| 查询缓存与回调数据 | 外部 Query 和 Borrow 按顺序释放后关闭推进，内部缓存先于原生 World 销毁。安装资源退出后，回调仍可使用由 World 或父 Scope 依赖持有的数据 |
| 宿主整体关闭顺序 | Gameplay 依赖 ECS，World 保存真实 Gameplay 回调但不额外持逐 World Pin 时，全部 World 的销毁先于 Module 关闭/原生卸载，无外部阻塞时能够完成 |
| 多提供方、World 与宿主 | 同一 World 的多个提供方持续有效；关闭单个 World 不卸载提供方；关闭一个宿主不破坏其他宿主的 World 或动态库使用 |
| 失败回滚 | 提供方已经安装组件/回调后失败，仍在提供方代码有效期间销毁失败 World；已有 World 保持可用 |
| 原生卸载顺序 | Modular 使用由宿主加载且应用未直接链接的 Gameplay DLL，证明组件析构、回调及上下文释放在该 DLL 的 NativeUnload 之前。ECS 作为应用链接持有的常驻 Module 验证其关闭顺序 |
| 清理失败与重试 | World Borrow、Stop/Poll 错误保持宿主停留在 World 阶段且不停止父 Scope Provider；外部 CodePin 不阻止 World 排空，但阻止后续原生卸载；各自释放/恢复后重试可推进 |
| 接纳与未交付 Scope | Closing 拒绝已发令牌的新 Borrow、新 Scope 和新业务 Pin；创建失败的残留 World 同样排空；重复推进不重复销毁 |
| 原有行为保护 | 独立 ModuleManager 的依赖清理、外部 Pin、原生释放失败及失败批次 Provider 屏障保持有效；没有 World 的宿主沿原有 Module/Log 关闭路径完成 |
| 链接与部署 | Modular 的应用和业务 DLL 操作同一实际 flecs runtime；Monolithic 最终链接，消费头配置和部署依赖一致 |

实现后使用正式 Preset 与隔离 BuildRoot 执行受影响检查：三桌面分别运行两种链接模式的 ECS/Module/Host 必要测试，移动平台完成两种模式真实最终链接及产物检查，明确没有设备运行。运行集合与全量范围由实施计划根据实际改动确定；相同已测 tree 的整合继续按 AGENTS.md 复用，不因提交号或分支名变化重跑全量。

## 9. 留给实施者的选择

以下是本稿明确允许实施者在不改变行为契约的前提下决定的事项：公开类与辅助入口的最终名称/签名、公共命名空间头文件名、稳定 Module/Declaration 身份值、宿主内部关闭阶段及最小 Module/Scope 支持入口的具体表达、Query 缓存的测试载体、满足 v4.2.0 首版能力要求的最小构建选项、测试目标与分类接线。

不能由实施细节默默改变的事项包括：业务允许原生 flecs API、原生 API 统一经 `pg::flecs` 访问且不将类型直接引入 `pg` 根命名空间、每个 World Scope 一个实例、宿主保持代码有效至全部 World 清理结束、World 排空先于 Module 关闭、无外部阻塞时整体关闭必须完成、父 Scope Provider 在 World 排空期间有效，以及保留 World 热卸载属于后续能力。

## 10. 设计审阅与下一步

2026-11-17 初稿独立 Builder Check 结论为 `nothing important left`；该检查对应先前的逐 World Pin/可声明预清理方案，不作为本次宿主关闭顺序修订的复核结论。这只是设计可规划性检查，不代表源码评审、用户批准或运行验收。

本次宿主关闭顺序修订另经独立 Builder Check，结论同为 `nothing important left`：未发现编制实施计划前必须补充的重要问题；该结论不代表关闭阶段已经实现或测试通过。

2026-11-18 完成设计讨论，用户确认 Q1–Q20 及首版契约。第 3 至 9 节记录已确认的行为边界，具体接口与实施步骤留给实施计划。首版实现及正式验收待完成。

命名空间入口按用户后续要求补入第 4.1 节及验收清单；上述 Builder Check 记录对应此项补充前的设计，不外推为公共类型入口的编译或实现验收。

命名空间文档示例使用本地 flecs v4.2.0，在 Windows Clang 23.1.5、C++23、无异常及保留 RTTI 的最小上下文中验证：clang-format、公共头自包含编译、原生类型相同性及 `pg::ecs`/`pg::log` 共存检查通过；直接从 `pg` 根访问原生 entity 的负例按预期编译失败。单独对公共头执行 clang-tidy 无错误，但有 1 条 `misc-unused-alias-decls` 提示，因为该最小检查上下文没有消费者使用别名；实际目标仍需结合消费者编译数据库复核。文档片段验证不替代正式公共头或跨平台验收。

flecs 实现核对以 [v4.2.0 固定提交](https://github.com/SanderMertens/flecs/tree/6566d4b7bdc597d0b74d5e0a4104d4ae6ac49402)为准，Pangu 侧以当前源码为准。上游解释性来源：[组件与 Hooks](https://www.flecs.dev/flecs/EntitiesComponents.html)、[C++ Hooks 示例](https://www.flecs.dev/flecs/examples-entities-hooks.html)、[flecs Modules](https://www.flecs.dev/flecs/group__cpp__addons__modules.html)。通用在线文档不替代固定版本源码；版本、配置、接口和行为均须按 v4.2.0 核对。
