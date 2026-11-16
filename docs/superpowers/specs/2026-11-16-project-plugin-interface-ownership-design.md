# Project 集成夹具的插件接口归属

状态：用户已选择方案一并要求实施（2026-11-16）。

## 目标与约束

Physics、Gameplay、EditorExtras、SharedProbe 各自拥有身份、Query 和 Service 的公开声明，消除插件对 MyGame/Source 的反向依赖。保留现有命名空间、身份值、行为、测试归属和两种链接模式。

每个插件的 Public 目录通过仅头文件的接口目标传递；实际 Module 目标链接该接口目标。身份头不依赖生成的导出头，Modular 宿主可读取身份和 Service 类型而不直接链接业务库。Query 头仍通过实际 Module 目标提供导出声明。

MyGame/Source 保留宿主入口、运行断言和候选装配；移除集中持有所有插件身份的 FixtureModules.h。Physics 不读取 MyGame 的源码路径，自身测试也遵守此边界。

## 方案取舍

- 方案一（已选择）：公开接口归属各插件，调整消费者使用要求；修改较多，但依赖方向明确。
- 方案二（未选择）：统一 FixtureContracts 目录；迁移少，但继续集中耦合各插件身份。

## 验收

在已有 PluginTestOwnership 的 Provider 场景加入独立 Physics 消费者，使用独立顶层源码目录构建并运行 Physics 自身测试。复用现有双模式宿主消费、插件选择、共享部署、增量和归属回归；检查 Modular 宿主没有新增业务库原生导入。按实际完成的平台记录验证，不将 Windows 结果当作跨平台验收。
