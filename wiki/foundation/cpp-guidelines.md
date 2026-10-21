# Pangu C++23 开发原则

> 状态：Partially Frozen（语言和核心工程原则已确定；具体类型/API 按子系统规范）
> 更新：2026-10-21
> 规范配置：仓库根目录 [`.clang-format`](../../.clang-format) 和 [`.clang-tidy`](../../.clang-tidy)

## 已确定

1. 使用 **C++23**；目标编译器、C++ 标准库和版本约束遵守 [统一工具链规范](toolchain.md)，不在 C++ 风格规范中重复维护版本矩阵。
2. 引擎代码命名空间以 `pg` 开头；自定义 Clang 属性使用 `pga` 属性命名空间。
3. **不使用 C++ Exception** 作为引擎的错误处理机制。
4. **Compile-time First**：能够通过类型系统、模板、Concept、`constexpr`、静态多态解决的场景优先静态方案；必要的运行时机制保留。
5. **Zero-overhead Abstraction**：将运行时成本视为需要证明合理性的设计约束；对性能关键路径使用测量和生成代码验证。
6. 强调类型安全、RAII、明确资源所有权、数据导向、减少不必要的分配与间接访问。
7. 对跨 C++ Module 的 STL 类型**不做一刀切禁止**；必要条件是 ABI、Allocator 和 Lifetime 契约兼容。
8. 反射/元数据声明不用 `PG_CLASS` / `PG_PROPERTY` 这类宏，详见 [Reflection / Meta](../core/reflection-meta.md)。

## 错误与分配策略

- 可恢复错误采用无堆分配的结构化 Error 与 `std::expected` 结果表达，并由调用链显式处理或传播；具体数据布局与 API 签名仍未冻结。Result 本身不承诺 OOM 可恢复，须由接口另行声明。
- 不可恢复失败进入所有构建配置有效的终止路径，不以 Debug 断言代替错误处理。
- 析构与释放不得依赖抛出异常；禁用异常下的 `std::pmr` 策略见 [内存管理](../core/memory-management.md)。
- C++ 标准库某些操作的默认失败语义与无异常编译约束存在交互，需要按采用的实现进行验证；不得声称所有 STL 操作都可以自动传播可恢复错误。

Result/Error、操作级失败报告、Assert/Check/Fatal 及日志生命周期的语义以 [错误处理与基础诊断](error-diagnostics.md) 为准（Frozen：设计语义）。具体 API 与实现状态独立追踪，不由设计冻结推定完成。

## 代码风格：当前工具配置

**实际风格以仓库配置文件为准**，下表反映当前已存在的设置，不替代完整配置。

| 项目 | 当前设置 |
| --- | --- |
| `clang-format` 基础 | LLVM |
| C++ 标准 | c++23 |
| 缩进 | 4 空格，禁用 Tab |
| 行宽 | 120 |
| 大括号 | Allman |
| 指针星号位置 | 类型侧，如 `int* value` |
| 类型名 | CamelCase |
| 函数名 | CamelCase |
| 普通变量 | camelBack |
| 命名空间 | lower_case |
| 常量 | `kCamelCase`（详见 clang-tidy） |

`.clang-tidy` 当前启用多组 bugprone、clang-analyzer、modernize、performance、portability、readability 与 cppcoreguidelines 检查，并保留显式排除项；`WarningsAsErrors` 当前为空。不要将尚未接入的 CI 规则描述为已强制执行。

## 实践原则

- 对所有权转移进行显式建模；裸指针或引用默认不能凭空保证对象存活。
- 组件存储与热路径的数据布局应和缓存/批处理需求共同设计。
- 模板和 Concept 用于表达静态契约，不为追求编译期而增加无意义的复杂度或编译成本。
- 虚函数、类型擦除、运行时分发在宿主/插件边界可以使用，但应明确理由和成本。
- 跨线程任务不允许随意捕获可能失效的局部借用；Module 卸载需协调任务与回调。
- 性能结论用 Profile、Benchmark 或反汇编证明，不凭代码外观判断。

## 尚未冻结

`Result/Error`、失败报告与断言/致命错误的具体 API 名称、签名和布局，具体容器替代方案、异常编译选项的每目标细节、RTTI 策略、C++ Modules（语言特性）的采用范围、公共头文件和源码文件的细粒度目录规范。这些不因本文件而被隐式确定；错误与诊断的既有语义决策不属于待决事项。
