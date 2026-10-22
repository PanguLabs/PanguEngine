# ADR-0010：STATIC / SHARED 统一 C++23 Module 发现与注册接口

> Status: Accepted（发现/注册技术路线；精确 Public API / 真实装载实现尚未冻结）
> 日期：2026-10-21
> 权威接口规范：[ModuleDescriptor & Registration API v1](../core/module-api.md)
> 依赖：[ADR-0008 Module Registration & Lifetime](0008-module-registration-lifetime.md)、[ADR-0009 ABI Profile / Fingerprint v1](0009-abi-profile-fingerprint.md)
> **修订范围**：替代 ADR-0008 Decision 第 2 条及 ADR-0009 Decision 第 6 条中要求 SHARED 通过“窄 C 兼容 Discovery 数据/POD”进行首次交换的部分；其他冻结内容继续有效。

## Context

先前 Module Registration & Lifetime v1 为 SHARED 规定窄 C Discovery → ABI 校验 → 原生 C++ Registration；这为 ABI 不一致时提供受限发现面，但额外引入独立的 C POD、注册桥、数值错误转换和两种业务侧入口形式。

PanguEngine 是受控 C++23 + Clang 构建的引擎：同目标进程的 C++ Module ABI 必须由真实 Toolchain Profile 和 ABI Fingerprint v1 严格校验，且允许兼容时直接交换 STL。参考 UE5 和 O3DE 均使用原生 C++ Module 对象，动态查找可以借助稳定导出名称。决定在仍要求**装载前可信预检**的前提下，消除额外的 C POD Discovery 层，并使 STATIC/SHARED 共享统一 C++23 ModuleDefinition/Registration。

## Decision

1. **统一数据和行为**：STATIC/SHARED 共享原生 C++23 `pg::module::ModuleDefinition`、`ModuleDescriptor` 与 `RegistrationContext`、`RegisterModuleFn` 语义。Module 注册是可回滚事务，注册成功不代表 Service Ready；不要求统一 `IModule` 虚基类、`ExtensionDescriptor` 或 `BinaryImage`。
2. **STATIC**：CMake/PanguMeta 生成显式 Catalog，通过按 Module 唯一化的 C++ 查询函数指针直接引用 ModuleDefinition，并在最终链接/装配阶段执行受控 ABI/Profile 和接口契约验证；不依赖全局静态初始化顺序。
3. **SHARED**：使用一个版本化稳定名称的导出符号（候选 `PanguModuleQueryV1`），以 `extern "C"` 保证符号可被 `GetProcAddress/dlsym` 按名称查找；该导出函数**返回原生 C++23 `ModuleDefinition` 指针**。C Language Linkage 用于**符号定位**，不意味着返回数据拥有独立的稳定 C ABI。包装代码由构建生成，开发者不需手写业务标记宏。
4. **不用独立 C Discovery**：取消 `PgModuleDiscoveryV1` / `PgModuleDependencyV1`、C 状态码转换和 `void*` `RegistrationContext` Bridge。发现后通过同一个 C++ RegisterModuleFn 执行事务注册，遵守 ADR-0007 的结构化 Error/Result 约束。
5. **强制受控的 Preflight**：在 **OS Load 和首次 C++ Query 之前**，必须基于与实际加载产物可验证绑定的非执行元数据完成目标平台、ABI Profile v1、完整 Fingerprint、Query/ModuleDefinition Public Module API Contract 及协议版本一致性验证。未验证、无法将预检结果绑定到加载产物、无法解决 TOCTOU，或受控 C++ ABI/API 不兼容时应 **Fail Closed**，不得尝试 C++ Query。ABI Fingerprint 不等于接口签名、包签名或恶意代码安全保证。预检元数据的嵌入/绑定工艺仍待冻结，但不得以信任可替换 Sidecar 代替产物身份验证。
6. **装载后复核**：OS Load 成功后才获取稳定导出符号，在已验证兼容的 C++ ABI 下调用 Query；返回的 ModuleDefinition 必须继续核验真实 Profile、Canonical/Digest、ModuleId、依赖及接口契约与预检材料一致，全部通过才能调用 C++ 注册入口。Host 深复制必要 Descriptor 数据，保留驻留保护并按既定 Module 生命周期卸载。
7. **代码驻留**：由 ModuleManager 管理实际 OS 库句柄，ModuleCodePin 保护代码，ServiceRef/Borrow 保证实例；注册失败回滚、Quiesce/Drain、Busy 保留模块身份和库句柄等仍遵循 ADR-0008。一个 Module 仍只对应一个 STATIC/SHARED Target。
8. **约束**：本决策不改变 ABI Profile v1 的 JSON Schema、RFC 8785、SHA-256 Fingerprint 或 Strict-v1 校验策略。也不保证 `LoadLibrary/dlopen` 的 DLL 初始化/静态构造不先执行；预检不替代供应链安全或沙箱。

## Consequences

- 减少两套 Descriptor/Registration 协议；业务入口可直接使用 C++23 强类型和受控 STL，而无需 C ABI 转换。
- `extern "C"` 仍用于可移植的**固定导出符号名**，不推行通用 C Bootstrap，也不依赖跨平台 C++ Mangled Name。
- 对 **Preflight 可信产物绑定、TOCTOU、防止在兼容性校验前解释不兼容 C++ 类型** 的要求提高。未完成这些条件时，**SHARED C++ Query 路线尚不可投入生产**，不得默默降级为“直接加载再比较 Fingerprint”。
- 该路线的接口 ABI/导出符号、C++ Function Language Linkage、真实跨平台 Load/Query 和元数据可信绑定仍待 C++23 探针与正式测试验收。
- 借鉴 O3DE 的固定符号定位方法，**不照搬**其统一 `AZ::Module` 基类及 Module 对象创建/销毁机制；Pangu 使用自由函数、ModuleDefinition 与 RegistrationContext。

## 验收与非冻结事项

在 Windows/WSL Linux/macOS 使用真实编译产物验证静态根引用、单一稳定符号、C++ Query/Registration、预检失败不进入 Query、ABI/API 改变被拒、预检/装载后的同一产物绑定、异常/RTTI 策略、受控 STL 交换与卸载 CodePin。Android/iOS 先验证目标链接和可用装载策略，不以交叉编译等同真机运行。

**尚未冻结**：精确导出符号名称与调用约定、ModuleDefinition/ModuleDescriptor 的 C++ 类型布局、Public Module API Contract/Digest 的表示、元数据嵌入/绑定及 TOCTOU 的实现技术、ModuleId 编码、Plugin Manifest、完整 Hot Reload。当前是**技术路线 Accepted，不是实现已验收**。

后续更新：[ADR-0011](0011-module-api-behavior-contract.md) 已冻结 ModuleId 的值/文本表示及 Module API 的行为契约，接续本节相应未决项；C++ 对象布局、精确 API/ABI 和生产 Loader 仍待验证。
