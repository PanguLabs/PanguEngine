# PanguEngine 总体架构基线

> 状态：Partially Frozen（技术基线与职责分层冻结；具体接口仍待完善）
> 更新：2026-10-21

## 目标与范围

PanguEngine 是基于 C++23 的自研跨平台游戏引擎。设计目标平台为 Windows、Linux、macOS、Android、iOS，全部平台使用 Clang 系列工具链，Windows/Linux/macOS/iOS 采用 LLVM Clang 23.x，Android 使用 NDK 自带 Clang；语言标准均为 C++23。Windows 是主要开发环境。

本文件记录**长期有效的顶层约束**，不是组件已交付或全平台测试通过的声明。规范以对应的 Core / Runtime 设计文档为准，重大决策的理由见 [decisions](decisions/README.md)。

## 逻辑分层

```text
Game / Editor Applications
          |
Runtime Modules / Services (ECS, Rendering, Assets, Input, ...)
          |
Core Architecture (EngineHost, Plugin/Module, Meta, Memory, ...)
          |
Foundation (C++23, Clang, CMake, Platform, ABI)
```

- **Foundation** 解决“如何编译、构建和在目标平台保持兼容”。
- **Core** 解决“引擎如何启动、扩展、管理类型、资源和生命周期”。
- **Runtime Modules / Services** 按功能领域组织游戏开发能力；不采用统一 Subsystem 基类或 SubsystemManager。采用 Concrete / Contract / Registry Service 三种能力模式：静态服务直接强类型访问，动态 Contract 通过 Scoped Registry，业务多 Provider 通过 Module 自有 Registry。
- **Applications** 是使用引擎能力的应用形态；Editor 和 Game 不构成两个互不兼容的引擎核心。

以上是**文档和职责分层**；各目录/职责层不与库一一对应。但**单个 Pangu Module 已冻结为一个 CMake STATIC/SHARED Library Target 和一个主要库产物**，详见 [Plugin / Module](core/plugin-module.md)。

## 已确定的技术选择

| 主题 | 决策 | 权威文档 |
| --- | --- | --- |
| 语言 | C++23；禁用 C++ Exception | [C++ 规范](foundation/cpp-guidelines.md) |
| 编译器/标准库 | Windows/Linux/macOS/iOS LLVM Clang 23.x，Android NDK 内置 Clang；Windows 使用 MSVC STL，其他平台使用 `libc++`；不使用 `libstdc++`；不 Fork llvm-project | [工具链](foundation/toolchain.md) |
| 构建 | 采用 CMake 4.0+，一个 Module 对应一个 STATIC/SHARED 库 Target 和一个主要产物 | [构建系统](foundation/build-system.md) |
| ABI | 同一受控构建的兼容 C++ Module 可交换 STL 类型；必须遵循所有权契约 | [ABI](foundation/abi.md) |
| 命名空间 | C++ 使用 pg 开头的命名空间；扩展属性使用 pga | [C++ 规范](foundation/cpp-guidelines.md) |
| ECS | 选择 flecs | [Runtime](runtime/README.md) |
| Rendering | 基础图形库选择 The Forge | [Runtime](runtime/README.md) |
| Host | 对外概念采用 EngineHost，而不是容易与 Editor Runtime 混淆的 RuntimeHost | [Engine Host](core/engine-host.md) |
| Service | 不使用 Subsystem 架构；采用 Host/Context/World Scope 与 Concrete / Contract / Registry Service 三种模式 | [Service](core/service.md) |
| 反射声明 | 使用 pga Clang 属性，不以 PG_CLASS 等宏标记 | [反射/元数据](core/reflection-meta.md) |
| 内存 | 明确来源、所有权、生命周期与安全释放 | [内存管理](core/memory-management.md) |
| 错误与诊断 | 无堆分配值错误 + `std::expected`；重要操作独立失败报告；有界日志与 Fatal 应急路径分离 | [错误处理与基础诊断](foundation/error-diagnostics.md) |

## 跨领域设计原则

以下概括已确定的通用取向和建议实践；完整评审规则见 [C++ 规范与最佳实践](foundation/cpp-guidelines.md)，领域内被冻结的细节仍以相应专题文档为准。

1. **Compile-Time First / Build-Time Validation**：类型、分发与稳定的元数据尽量提前确定；CMake 依赖与构建配置前置校验，真正动态的 Plugin Provider、外部输入、设备能力和 World 状态留给运行时。
2. **Zero-Overhead Abstraction**：抽象自身不应无故增加 Runtime 成本；不等于遍历、分配、调度、Handle 校验或 GPU 同步无成本。性能结论须测量。
3. **Type Safety / Value Semantics**：优先强类型 ID、值类型、Const、静态分发；在确有需要时使用动态分发，不为静态化而制造过度模板复杂度。
4. **Data-Oriented / ECS + Event-Driven**：flecs 是既有选型；Component/数据布局围绕访问模式设计，Event/Command/Message 倾向类型安全；具体 Query、消息语义和调度 API 尚未冻结。
5. **Explicit Ownership / Lifetime / Scheduling**：句柄、借用、异步回调与跨 Module 分配/析构须有明确存活保护；Task/Job 并行应显式声明依赖和收尾条件。
6. **Determinism / No Hidden Work**：对需重放的模拟显式控制步长、输入和顺序；API 应声明 I/O、分配、阻塞和 GPU 同步，不承诺跨平台浮点逐位一致。
7. **Exception-Free / Observable / Testable**：可恢复失败用值错误显式传播；可选日志与 Trace 不承担正确性；系统应可测试、可测量且符合五平台差异。
8. **选型与设计不等于实现**：flecs、The Forge、Meta 生成、ECS Scheduler、事件体系的选择或建议不表示其接口已经冻结或通过完整集成验收。

## 对外边界

- 一个 Module 对应一个实际 STATIC/SHARED Library Target，只产生一个主要库产物；动态库句柄与 CodePin 由 Module 机制管理，不再引入独立 BinaryImage 架构层。STATIC Module 的代码随最终承载它的 EXE/动态库驻留，见 [Plugin / Module](core/plugin-module.md)。
- 对于跨 C++ Module 的传值，ABI 兼容、分配器来源、对象及析构代码所在 Module 的有效期均为约束，详见 [ABI](foundation/abi.md) 和 [内存管理](core/memory-management.md)。
- 动态模块卸载前必须停止新的调用并处理任务、回调、资源及 GPU 完成状态；不能直接卸载存活对象仍依赖的代码。

## 未冻结的顶层事项

ECS 与 Object/Meta 的组合、Scheduler、Job、Asset、Editor UI、事件体系、序列化格式、VFS、Networking、可执行程序布局、具体运行时服务 API 和目录组织，尚需独立规范或更新后的设计记录。No Subsystem / Scoped Service 的设计原则见 [Service](core/service.md)，当前 PoC 签名不视为已冻结 API。

错误返回、失败报告、断言与基础日志的设计语义已由 [错误处理与基础诊断](foundation/error-diagnostics.md) 冻结；具体 API、布局、容量及平台实现仍待设计，不属于已完成的 Runtime 能力。

## 设计状态与实现状态

- **设计**：上文明确列出的技术方向可作为后续规范的前提；部分子系统文档仍标记为 Partially Frozen。
- **实现**：本文件不声称存在 EngineHost、构建目标、Renderer 或五平台 CI 实现。检查源码及测试后才能更新实现状态。
