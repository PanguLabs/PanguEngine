# Pangu C++ ABI 基线

> 状态：Partially Frozen（普通同树构建不经 AbiProfile 由 ADR-0013 冻结；Pangu 原生 Plugin 改为源码交付由 ADR-0014 冻结；历史 strict-v1 已退役）
> 当前边界：[ADR-0013](../decisions/0013-native-build-abi-boundary.md)、[ADR-0014](../decisions/0014-source-plugins-project-build.md)；历史协议：[ADR-0009](../decisions/0009-abi-profile-fingerprint.md)

源码 Plugin/独立 Project 最小接线的编译、链接和显式部署证据见[2026-11-04 验收](../../docs/superpowers/plans/2026-11-04-source-plugins-project-build-validation.md)。这些历史构建验证不证明本轮 ModuleManager/Loader 矩阵、设备运行或跨进程二进制 ABI 兼容。
> 更新：2026-11-07

## 范围

本文的 ABI 边界指 **同一目标进程内**、可能分别构建/加载的 C++ Module 或动态镜像之间的二进制交互。各操作系统、架构及工具链目标的 ABI 分别处理；不声称存在跨 Windows/Linux/macOS/Android/iOS 通用的单一 C++ 二进制 ABI。

## 已确定的原则

1. 进程内的编译器、C++ 标准库选型遵守 [统一工具链规范](toolchain.md)；但相同编译器品牌、主版本或标准库名称仍不足以证明 ABI 兼容，必须验证真实构建配置。
2. 允许同一受控构建且配置兼容的模块通过 C++ 接口交换标准库类型（如 `std::string`、`std::vector`、`std::shared_ptr`、`std::function`）；不人为一刀切禁止。
3. 跨 Module 的 ABI 契约必须覆盖编译器/标准库及关键构建选项、对象布局和调用约定的兼容性。
4. 同一构建树、同一配置产生的源码库、普通 EXE、Tests 及与宿主配套交付的 Pangu Plugin 不经过 AbiProfile。根据 ADR-0014，**不支持**向既有 Host 单独交付 Pangu 原生 Plugin/SDK 二进制，也不实施其可信 Preflight；普通第三方预编译 SDK、系统库仍需按 Toolchain 和真实 ABI/Ownership 约束审查。历史 strict-v1 仅用于解释旧产物。
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

这些兼容性维度仍适用于普通 ThirdParty/系统二进制的技术审查和跨 Module 风险分析，但不代表 Pangu 要开发独立 SDK/Plugin 接纳协议。历史 AbiProfile v1 的字段表、规范化规则和指纹算法保留在 [ABI Profile / Fingerprint v1](abi-profile-fingerprint.md) 及其 [JSON Schema](abi-profile-v1.schema.json) 中，仅作历史资料；不能要求普通构建恢复逐 TU 收据或 strict-v1。

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

当前已实现 ModuleManager 的 Query/运行时检查和配套部署；平台证据分别记录，热重载及 Runtime/Public ABI 细节仍未冻结；**独立 Pangu SDK/Plugin 的 config-v2、可信装载前 Preflight、产物绑定与 TOCTOU 方案不再规划**。普通第三方 SDK 的兼容性由其引入方审核。五平台工具链/标准库仍遵循 Toolchain 规范；strict-v1 已退役。

后续若记录这些决策，需要更新本文件并提交相应 ADR；不得直接从过往示例代码认定已经生效。

## ModuleManager 的代码与控制块寿命（2026-11-06）

依据 [ADR-0015](../decisions/0015-unified-link-modes-module-manager.md)，保留 RTTI 并在内部擦除处比较 type_info 值；不能使用地址、仅名字/hash 或人工类型身份代替。业务 DLL RTTI、对象、匹配 Deleter、Factory/捕获和回调在卸载前解除；ScopeAccess/Pin 控制块、闸门与最终释放入口位于宿主常驻 Runtime，残留访问不延长实例寿命，关闭后 TryBorrow 不读取 DLL 数据。

ModuleManager 自身原生持有与外部 Pin、Borrow、内部资源依赖分别记账；最后业务线程 Release 不执行 OS 卸载。先按有效资源依赖排空/销毁，再退休工厂并等待对应 Pin，原生释放成功才失效记录；释放未确认前保留 Provider。常驻记录最后统一确认且不释放宿主原生持有。成功不保证额外 OS 引用也消失。实际 RTTI、真实物理卸载和移动端链接层次见[正式验收](../../docs/superpowers/plans/2026-11-06-module-manager-link-modes-validation.md)，不推导设备或跨配置 ABI 已通过。

POSIX 受控同配置 Module 使用 RTLD_NOW|RTLD_GLOBAL，使独立装载组的公开 RTTI 可参与同一进程符号解析；LOCAL 下两桌面真实合法依赖已失败，GLOBAL 对照恢复。仍比较 std::type_info 值，保留最小公共导出/私有隐藏。公开符号进入全局作用域会增加名称冲突和符号插入风险，须遵守 ABI/ODR，不能作为插件隔离。依据 [libc++ 可见性](https://libcxx.llvm.org/DesignDocs/VisibilityMacros.html)、[POSIX dlopen](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dlopen.html)及 [dlclose](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dlclose.html)；实际平台比较与物理卸载结果按各自矩阵记录，不扩大为任意工具链或外部预编译库保证。

ELF 可执行宿主参与同类 RTTI 匹配时，仅类型 visibility(default) 不保证符号进入主程序动态符号表；可用 CMake `ENABLE_EXPORTS` 建立公开符号的动态导出前提。Linux Modular 常驻 Factory 回归在同一实际链接命令增加 export-dynamic 后恢复；只对该测试 Host 开启，不全局修改其它目标或增加逐类型锚点。宿主是否参与公共类型交互须按实际用途判定，库的显式导出与私有隐藏仍独立保留。
