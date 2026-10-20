# ADR-0005：一个 Module 对应一个静态库或动态库

> Status: Accepted
> 日期：2026-10-20
> 权威规范：[Plugin / Module](../core/plugin-module.md)、[Build System](../foundation/build-system.md)

## Context

此前 Plugin / Module 方案允许逻辑 Module、多个 CMake Target 和动态镜像之间存在复杂映射，并额外讨论了 BinaryImage 描述/管理层。这样的灵活性增加了 Module 身份、注册、ABI 校验与安全卸载的协同成本。

当前选择使用 CMake 4.0+，希望每个逻辑 Module 是独立的代码构建与库交付单位；Plugin 仍是上层功能及分发组织。

## Decision

1. 一个 Pangu Module 对应唯一一个**实际 CMake Library Target**，类型为 `STATIC` 或 `SHARED`；一个 Module 构建仅输出一个主要静态库或动态库。
2. 多个源文件/Translation Unit 可以共同形成这个 Module，不将“编译单元”误解为只有一个 `.cpp`。
3. CMake ALIAS、Windows import library / PDB 等不视为第二个 Module Target 或第二个主要库产物。
4. Plugin 可包含多个 Module，也可仅包含 Assets；ThirdParty / Host / Tool 目标不自动成为 Pangu Module。
5. 不建立独立的 `BinaryImage`、`BinaryImageManager` 或类似描述层。Module 加载机制直接管理动态库句柄；CodePin 保证其实际所在代码镜像的驻留。
6. 静态 Module 的库归档本身不能单独卸载；当静态代码被链接进可卸载共享库时，销毁/回调代码驻留仍须跟随实际承载它的动态库。

## Consequences

- 相比早期“Module 不与 CMake Target 一对一”的开放映射，新的 Module 构建规则已经收紧。
- Module 注册、Meta 生成和链接必须针对同一 Library Target，不产生额外的 Module 库。
- 不减少既有 ABI / Allocator / Borrow / Task / GPU 安全卸载义务；物理动态库仍需要正常加载/卸载。
- Plugin Manifest、ModuleDescriptor、`pg_add_module()` 的具体 schema/API 和动态库符号入口仍待冻结。

## Open Questions

STATIC/SHARED 选择默认值、安装布局、Module ABI Manifest、动态装载 API、CMake 代码生成流程与平台运行时限制。
