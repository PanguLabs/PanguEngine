# Project 集成夹具的插件接口归属

状态：用户已选择方案一并要求实施（2026-11-16）。

## 目标与约束

Physics、Gameplay、EditorExtras、SharedProbe 各自拥有身份、Query 和 Service 的公开声明，消除插件对 MyGame/Source 的反向依赖。保留现有命名空间、身份值、行为、测试归属和两种链接模式。

2026-11-16 用户补充命名边界：`Pangu::` 仅由 `Source/Runtime/` 定义，其他目录可引用 Runtime 别名，但自己的 Module 和辅助目标使用普通名称。Physics 实际目标改为 `FixtureEnginePhysics`；MyGame 自有 Module 使用 `MyGameGameplay`、`MyGameEditorExtras`、`MyGameSharedProbe`；Toolchain 直接使用现有普通目标。本条替代前述对 CMake 目标名称的保留要求，C++ 命名空间、Module 身份和协议符号保持原有契约。

用户随后要求同步修改 Physics 夹具的 Plugin ID，统一使用 `fixture.physics`；启用列表、访问顺序、测试标签和相关夹具同步此 ID，来源仍为 engine，Runtime ModuleId 保持原值。

Physics 自身测试目标进一步按用户要求统一为 `FixtureEnginePhysicsPluginTests`，同步其 CTest 名称前缀和构建/筛选/归属检查，测试行为及分类保留。

每个插件的 Public 目录通过仅头文件的接口目标传递；实际 Module 目标链接该接口目标。身份头不依赖生成的导出头，Modular 宿主可读取身份和 Service 类型而不直接链接业务库。Query 头仍通过实际 Module 目标提供导出声明。

MyGame/Source 保留宿主入口、运行断言和候选装配；移除集中持有所有插件身份的 FixtureModules.h。Physics 不读取 MyGame 的源码路径，自身测试也遵守此边界。

## 方案取舍

- 方案一（已选择）：公开接口归属各插件，调整消费者使用要求；修改较多，但依赖方向明确。
- 方案二（未选择）：统一 FixtureContracts 目录；迁移少，但继续集中耦合各插件身份。

## 验收

在已有 PluginTestOwnership 的 Provider 场景加入独立 Physics 消费者，使用独立顶层源码目录构建并运行 Physics 自身测试。复用现有双模式宿主消费、插件选择、共享部署、增量和归属回归；检查 Modular 宿主没有新增业务库原生导入。按实际完成的平台记录验证，不将 Windows 结果当作跨平台验收。
