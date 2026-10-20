# Pangu C++ ABI 基线

> 状态：Partially Frozen（进程内兼容性策略与 Ownership 原则已确定；Fingerprint 格式、算法和平台 Profile 未冻结）
> 更新：2026-10-20

## 范围

本文的 ABI 边界指 **同一目标进程内**、可能分别构建/加载的 C++ Module 或动态镜像之间的二进制交互。各操作系统、架构及工具链目标的 ABI 分别处理；不声称存在跨 Windows/Linux/macOS/Android/iOS 通用的单一 C++ 二进制 ABI。

## 已确定的原则

1. PanguEngine 全平台以 Clang 作为编译器；**Windows 使用 Clang + MSVC STL，Linux、macOS、Android、iOS 使用 Clang + `libc++`**。所有目标平台均不使用 `libstdc++`；但编译器品牌相同不足以证明 ABI 兼容。
2. 允许同一受控构建且配置兼容的模块通过 C++ 接口交换标准库类型（如 `std::string`、`std::vector`、`std::shared_ptr`、`std::function`）；不人为一刀切禁止。
3. 跨 Module 的 ABI 契约必须覆盖编译器/标准库及关键构建选项、对象布局和调用约定的兼容性。
4. 需要兼容性检查，防止不兼容模块进入同一进程；历史方案名为 `AbiProfile` / `AbiFingerprint`，**具体字段及哈希算法尚未正式冻结**。
5. 引擎不使用 C++ Exception；Module 边界也不得依赖异常跨越。
6. **内存所有权独立于 ABI 一致性**：即便 C++ 类型布局兼容，也必须知道由谁分配、析构和释放。
7. 动态 Module 卸载之前，必须确认依赖其代码、deleter、Allocator、资源或回调的对象不再存活。
8. 必要时可采用明确的 C ABI 边界（函数指针 + 不透明上下文）；这不是对所有 C++ Module 的强制改造。

## 已冻结的编译器与标准库组合

| 平台 | 编译器 | C++ 标准库 |
| --- | --- | --- |
| Windows | Clang | MSVC STL |
| Linux | Clang | `libc++` |
| macOS | Clang | `libc++` |
| Android | Clang | `libc++` |
| iOS | Clang | `libc++` |

以上是**项目选型约束**，不代表五个平台可以互换二进制产物。标准库的具体版本、ABI 选项及运行时链接方式仍必须由平台工具链验证。

## 所有权优先的跨 Module 约束

- Owning Object 的析构策略必须随所有权契约一起定义。
- 在另一 Module 分配的存储，不能未经契约授权就用本 Module 的 CRT/Allocator 释放。
- 共享智能指针和函数对象可能持有由其他 Module 编译的 deleter/callback；**类型本身 ABI 兼容并不保证卸载安全**。
- `std::pmr` 对象还需遵守其 backing allocator 和资源对象的生命周期限制。
- `std::string_view`、`span`、引用和裸指针均不自动延长数据存活。
- 对跨稳定 C ABI 的交换不使用 STL/PMR 容器作为 ABI 数据格式；受控 C++ 边界的约束是另一回事。

详见 [Pangu Memory Ownership Contract v1](../core/memory-management.md)。

## ABI 兼容性维度（待规范化）

以下仅列举实现校验时**需要考虑**的维度，不是已冻结的 Profile 字段表：

| 领域 | 示例风险 |
| --- | --- |
| 目标 | target triple、CPU 架构、调用约定、平台对象格式 |
| 工具链 | Clang 版本、编译前端行为与选项 |
| 标准库 | Windows 固定使用 MSVC STL；Linux/macOS/Android/iOS 固定使用 `libc++`；应校验其实际版本、ABI 配置和构建模式 |
| Runtime | CRT 链接方式、Debug/Release Runtime、Allocator 来源 |
| 语言选项 | RTTI、异常模式、结构体布局、packing、可见性、对齐 |
| 链接 | 导出符号、链接依赖、动态加载规则 |
| 构建产物 | 宿主/模块实际匹配的运行时 Manifest |

校验实现应从真实构建配置生成兼容性信息，不信任手工填写的、与实际编译不一致的标记。

## 未冻结的内容

`AbiProfile` 的字段、默认值、规范化序列化、Fingerprint 哈希算法、兼容性判定规则、`.pgmodule` Manifest 具体格式、RTTI/visibility/Class Layout 的逐平台硬性配置、各平台标准库的具体版本/ABI 选项/运行库链接方式，以及静态与动态镜像之间的完整装载协议。**五个平台的标准库选型已冻结，不属于待决事项。**

后续若记录这些决策，需要更新本文件并提交相应 ADR；不得直接从过往示例代码认定已经生效。
