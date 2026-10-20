# Pangu EngineHost 设计基线

> 状态：Partially Frozen（宿主职责、与三种 Service 模式的边界冻结；启动与生命周期 API 未冻结）
> 更新：2026-10-20
> 决策：[ADR-0002](../decisions/0002-name-engine-host.md)

## 背景

PanguEngine 需要同时支持 Game、Editor 以及工具型宿主。早期使用 `RuntimeHost` 命名，但它可能被误解为只负责 Game Runtime，未来存在 Editor Runtime 时容易产生歧义。

## 已确定

1. 顶层宿主概念使用 **`EngineHost`**；不以 `RuntimeHost` 作为统一抽象名称。
2. `EngineHost` 应表达**引擎宿主职责**，而不是把 Editor 自身视为另一套不兼容的引擎基础设施。
3. EngineHost 所依赖的 Module / Plugin 系统应能区分 Module 的安装/发现、加载、初始化、执行及停止/卸载等生命周期阶段。
4. 具体 Game / Editor 应用可以为不同运行场景选择不同能力、服务和启动策略；不能据此推断必须使用不同的宿主核心。
5. Host 负责或协调依赖对象的生命周期闭包：Module 代码、Task、Callback、Resource、Allocator 及 GPU 工作都可能影响关闭或卸载的完成时机。
6. 不采用**强制 C Bootstrap** 作为整套引擎架构的基础；必要的 ABI 边界互操作仍可使用 C 接口。

以上不意味着每一个生命周期阶段都有固定枚举名或必须由单个类直接执行。

## 概念关系

```text
Game / Editor / Tool Application
             |
         EngineHost
             |
   Plugin / Module Coordination
             |
Engine Services and Runtime Modules
```

这是依赖与职责示意，**不是已经冻结的类图**。EngineHost 不应被假定具有无限的职责；核心静态服务通过宿主/Context 直接强类型访问；动态扩展能力通过 Scoped ServiceRegistry 注册和借用。具体对象成员、工厂签名和依赖布局仍需要在 API 设计阶段讨论，见 [Service 架构](service.md)。

## 与 Plugin / Module 的关系

- EngineHost 按运行场景协调 Module 能力、依赖及安全收尾。
- Plugin 是发现、启用、分发和扩展的单元；Module 是代码组织、依赖和执行生命周期的单元，见 [Plugin/Module](plugin-module.md)。
- 每个 Module 对应一个实际 CMake STATIC/SHARED Library Target、一个主要库产物；Module 是否采用 SHARED 不等于它的 Service 必须运行时动态绑定，详见 [Plugin/Module](plugin-module.md)。
- EngineHost / EngineContext / WorldContext 对 Concrete Service 强类型持有；Contract Service 的动态 Provider 由 Scoped Registry 管理；Registry Service 是业务 Module 自己提供的多 Contributor Registry，不引入 ExtensionManager。
- Module 的加载、注册与 Service 的 Ready 是不同阶段；Host 负责或协调依赖闭包，不把它们统统交给一个万能 ServiceManager。
- 动态库句柄和 CodePin 由现有 Module 机制维护，不另建 BinaryImage 管理层；卸载必须满足 [内存所有权规范](memory-management.md) 的 Module 卸载前置条件。

## 尚未冻结

`EngineHost` C++ 类签名、HostConfig、Bootstrap 流程、初始化图的具体节点与编排、ServiceRegistry 的具体 C++ API 与 Host 集成、Host 与 Application 的所有权方向、多个 EngineHost 实例、Editor 与 Game 的宿主差异、错误传播和停机接口、Hot Reload 的完整状态机。Service 依赖 DAG、Scope 方向及逆序销毁语义已由 [Service](service.md) 冻结，不属于此处待决事项。

**实现准则**：如果设计需要新增上述约束，应先在规范或 ADR 中说明，而不能将本文件解读为已批准某个具体 API。
