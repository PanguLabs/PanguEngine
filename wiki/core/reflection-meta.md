# Pangu Reflection / Meta 声明基线

> 状态：Partially Frozen（属性命名方式、无宏原则和 Clang 政策已确定；Taxonomy 与生成协议待冻结）
> 更新：2026-10-19

## 已确定

1. 引擎需要元数据/反射支持以服务对象、ECS、序列化、事件和资源等系统。
2. **不使用 `PG_CLASS`、`PG_PROPERTY` 等反射声明宏**作为面向用户的主要声明机制。
3. 采用 **`pga` 命名空间的 Clang 扩展属性**方向，示意写法为 `[[pga::...]]`。
4. 引擎 C++ 类型的命名空间使用 `pg` 开头；`pga` 指属性声明域，两个用途不混淆。
5. 不 Fork `llvm-project`；元数据提取以官方 Clang 能提供的扩展/工具接口为基础。
6. 所有属性应规划成统一语义体系，避免 Object、ECS Component、Event、Message、Command、Serialization、Asset、Resource、Plugin 等领域各自发展不兼容语法。
7. 仅在确实需要动态反射、序列化或生成代码时产生额外机制；编译期可解决的问题优先编译期处理。

## 设计边界

| 职责 | 约定方向 |
| --- | --- |
| 源代码声明 | C++23 类型 + `pga` 属性 |
| 语义提取 | 借助 Clang 的 AST/语义能力，而非基于正则表达式解析 C++ |
| 规范化 | 校验声明并形成可供其他工具消费的 Meta 信息 |
| 代码生成 | 按需要生成注册、序列化、反射或系统集成代码 |
| 运行时 | 仅保留需要在运行时使用的元数据和注册信息 |

这里的 `PanguClang` / `PanguMeta` 仅是讨论中工具职责的名称；**尚不承诺两者是进程、库或 Clang Plugin 的固定划分**。

## 兼容性提示

官方 Clang 对未知属性、插件注册属性与 AST 呈现方式存在具体限制。示意语法不是已经验证过的可编译 Attribute Schema。落地前需明确“如何识别、验证和生成”，不能依靠不存在的编译器扩展行为。

## 与 Plugin / Module / Service 的关系

依据 [Plugin / Module](plugin-module.md) 与 [Service](service.md)，每个 Module 对应一个实际 STATIC/SHARED Library Target；Concrete / Contract / Registry Service 是该 Module 提供能力的三种模式。PanguMeta **可以**为必要的模块注册代码生成适配，但不强制让每个 Concrete Service 都产生运行时描述符，也不新增独立的 `ExtensionDescriptor` / `ExtensionManager`。

Registry Service 的多 Provider 贡献项通过已有业务 Registry 和 Module 注册/撤销机制管理；`pga::service` 或类似属性的完整语法尚未冻结，不能把历史示例作为必需声明。

## Taxonomy 待冻结范围

- Object / Type / Property / Function 的 Reflection 分类、Type ID 与生成策略。
- ECS Component / System 及其 Meta 之间的边界。
- Event / Message / Command / Delegate 的类型语义与注册方式。
- Serialization、版本演进与字段属性。
- Asset / Resource / Plugin/Module 属性和 Runtime Registry 关联。
- 属性参数语法、命名规则、错误诊断、兼容性演化。
- Generated Code、Schema、CMake 增量构建、模块边界的卸载/注销顺序。

这些属于后续 **Pangu Attribute Taxonomy v1** 和工具链契约，当前不强行定义 `pga::event`、`pga::component` 等未经正式确认的具体属性签名。

## 关联文档

[工具链](../foundation/toolchain.md)、[Plugin/Module](plugin-module.md)、[ABI](../foundation/abi.md)、[C++ 规范](../foundation/cpp-guidelines.md)。
