# ECS 与 flecs 集成

> 状态：Partially Frozen（首版 World 生命周期、原生借用和链接契约已确认并交付，Public C++ API/ABI 未冻结）。
> 更新：2026-11-18

## 已确认的方向

- ECS 采用 flecs，由独立 Runtime Module 管理引擎集成和 World 生命周期。
- 参考与首版接入基线为用户指定的 `v4.2.0`，固定提交见首版规格。
- 业务保留 flecs 原生类型、组件操作和强类型查询，接受依赖升级后的统一重编；不要求完整封装独立后端 API。
- `pg::flecs` 提供原生 API 的命名空间入口，`pg::ecs` 提供 Pangu 集成接口。业务在有效 `ScopeBorrow` 内操作原生 World。
- World 存活期间保护登记代码的提供方；先安全销毁 World，再允许卸载相关代码。保留 World 的 Gameplay 热卸载不属于首版。

实现遵循 [ThirdParty 边界](../foundation/third-party.md)、[Service](../core/service.md)、[Scope/Module 生命周期](../core/module-api.md)和[内存所有权](../core/memory-management.md)。

## 首版行为与实施边界

应用链接 `Pangu::ECS` 并通过 `HostStartupConfig::AdditionalResidentQueries` 接纳 `pg::ecs::QueryEcsModule`。每个 World Scope 由工厂拥有一个 `pg::ecs::World`，装配宿主统一的 World 声明。组件由定义方先登记，跨模块消费方在访问的每个 World 显式绑定并保留既有 Hooks。绑定依赖同一 World 内的组件身份，组件 ID 不跨 World 使用。

应用使用 `pg::ecs::AcquireWorld` 精确查询所属宿主的 World Scope，成功 `TryBorrow` 后才能经 `Native()` 访问 `pg::flecs::world&`。原生 World 始终归 Scope 所有。Query、Iterator、原生 World 副本及组件引用须在相应 Borrow 内使用并按原生生命周期规则释放。缓存 Query 的持有方在等待关闭前先销毁 Query，再释放 Borrow。实体值可保存，使用时须重新借用同一有效 World。

应用主线程串行登记、访问并显式调用 `Progress`。步长以秒计，必须有限且大于零，且转换为原生步长后仍为正值。成功返回 `false` 表示本次收到了 quit。运行系统、遍历和回调期间禁止重入 Scope/Host 关闭。所有配置保留上游常规断言。

整体关闭先停止新业务接纳，再排空本宿主全部 World，包括装配失败但尚未退休的 World。父 Context/Host 在此阶段仍维持原实例，Module 保持其原生持有，直到组件析构、回调和原生 World 销毁返回。业务代码驻留由宿主统一维护。只要仍有 World Borrow 或清理错误，宿主保留关闭阶段并允许重试。[ECS World 首版规格](../../docs/superpowers/specs/2026-11-17-ecs-world-design.md)

Windows、WSL Linux、macOS 验收两种链接模式下的实际运行，其中 Modular 核对真实共享 flecs 和业务模块卸载顺序。Android/iOS ARM64 双模式通过真实宿主链接及目标产物检查。设备运行、打包、签名和设备上的动态库卸载待验证。[ECS 测试入口](../../Tests/ECS/README.md) · [最终验收](../../docs/superpowers/plans/2026-11-18-ecs-world.md#ecs-交付验收)
