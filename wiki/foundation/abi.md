# Pangu C++ ABI 基线

> 状态：Partially Frozen（同树构建与独立交付的边界由 ADR-0013 冻结；历史 strict-v1 格式保留但已退役）
> 当前边界：[ADR-0013](../decisions/0013-native-build-abi-boundary.md)；历史协议：[ADR-0009](../decisions/0009-abi-profile-fingerprint.md)
> 更新：2026-11-02

## 范围

本文的 ABI 边界指 **同一目标进程内**、可能分别构建/加载的 C++ Module 或动态镜像之间的二进制交互。各操作系统、架构及工具链目标的 ABI 分别处理；不声称存在跨 Windows/Linux/macOS/Android/iOS 通用的单一 C++ 二进制 ABI。

## 已确定的原则

1. 进程内的编译器、C++ 标准库选型遵守 [统一工具链规范](toolchain.md)；但相同编译器品牌、主版本或标准库名称仍不足以证明 ABI 兼容，必须验证真实构建配置。
2. 允许同一受控构建且配置兼容的模块通过 C++ 接口交换标准库类型（如 `std::string`、`std::vector`、`std::shared_ptr`、`std::function`）；不人为一刀切禁止。
3. 跨 Module 的 ABI 契约必须覆盖编译器/标准库及关键构建选项、对象布局和调用约定的兼容性。
4. 同一构建树、同一配置产生的源码库、普通 EXE、Tests 及与宿主配套交付的插件不经过 AbiProfile；独立分发的 C++ SDK/预编译库和单独交付的原生插件才进入二进制接纳边界。历史 strict-v1 Schema/Canonical/Fingerprint 仅用于解释旧产物，不再是普通生产构建的活动协议。
5. 引擎不使用 C++ Exception；Module 边界也不得依赖异常跨越。
6. **内存所有权独立于 ABI 一致性**：即便 C++ 类型布局兼容，也必须知道由谁分配、析构和释放。
7. 动态 Module 卸载之前，必须确认依赖其代码、deleter、Allocator、资源或回调的对象不再存活。
8. 必要时可采用明确的 C ABI 边界（函数指针 + 不透明上下文）；这不是对所有 C++ Module 的强制改造。

## 编译器与标准库前提

五个平台的编译器和 C++ 标准库组合由 [Toolchain](toolchain.md) 唯一定义，不在 ABI 规范复制版本矩阵。**即使满足该组合，不同平台仍不得互换二进制产物**；标准库具体版本、ABI 选项和 Runtime 链接方式必须纳入实际构建的兼容性验证。

## 所有权优先的跨 Module 约束

- Owning Object 的析构策略必须随所有权契约一起定义。
- 在另一 Module 分配的存储，必须按 [内存所有权契约](../core/memory-management.md) 回到原分配域或明确可互相释放的等价分配域；ABI 兼容不能替代分配来源验证。
- 共享智能指针和函数对象可能持有由其他 Module 编译的 deleter/callback；**类型本身 ABI 兼容并不保证卸载安全**。
- `std::pmr` 对象还需遵守其 backing allocator 和资源对象的生命周期限制。
- `std::string_view`、`span`、引用和裸指针均不自动延长数据存活。
- 对跨稳定 C ABI 的交换不使用 STL/PMR 容器作为 ABI 数据格式；受控 C++ 边界的约束是另一回事。

详见 [Pangu Memory Ownership Contract v1](../core/memory-management.md)。

## ABI 兼容性维度与历史 v1 资料

独立交付 C++ 二进制仍需要覆盖下列兼容性维度，但下一版活动格式尚未冻结。历史 AbiProfile v1 的字段表、规范化规则和指纹算法保留在 [ABI Profile / Fingerprint v1](abi-profile-fingerprint.md) 及其 [JSON Schema](abi-profile-v1.schema.json) 中，仅作历史协议资料；不能据此要求普通构建恢复逐 TU 收据或 strict-v1。

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

独立交付二进制的下一版配置描述、机器可读 Manifest、原生 C++23 Query 的固定导出符号/公共接口 ABI、可信预检元数据嵌入/签名方式、公开类型的 API Surface Digest、SDK/运行库真实验收、RTTI/visibility/Class Layout 的跨平台细化约束，以及 Plugin 分发与热重载。五平台工具链/标准库选型仍按 Toolchain 规范；strict-v1 及其 STATIC/SHARED 校验阶段已经由 ADR-0013 从当前生产政策中退役。

后续若记录这些决策，需要更新本文件并提交相应 ADR；不得直接从过往示例代码认定已经生效。
