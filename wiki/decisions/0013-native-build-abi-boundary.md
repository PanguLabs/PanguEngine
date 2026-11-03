# ADR-0013：原生增量构建与独立二进制 ABI 交付边界

> Status: Accepted
> 日期：2026-11-02
> 替代：[ADR-0009 ABI Profile / Fingerprint v1](0009-abi-profile-fingerprint.md) 的生产适用范围，并部分修订 [ADR-0010](0010-unified-cpp-module-entry.md)
> 设计依据：[原生增量构建与 ABI 交付边界](../../docs/superpowers/specs/2026-11-02-native-build-abi-boundary-design.md)

## Context

旧 ABI v1 将逐 TU 证据、STATIC 最终组合校验和 SHARED 预检接入普通构建。实践中它重复扫描同一目标、生成大体积收据，并使验证器变化反向失效已有对象，显著增加 configure、编译和链接成本。PanguEngine 已使用 CMake + Ninja；源码和链接依赖应由真实 CMake Target 描述，增量失效由 Ninja 与编译器依赖信息负责，而不是由第二套收据系统重新判断。

同时，独立交付的 C++ 二进制仍可能与宿主由不同构建产生，不能因为都使用 Clang/C++23 就默认 ABI 或 Module API 可安全互操作。因此需要把“本地增量构建”和“独立二进制接纳”分成两个职责。

## Decision

1. **单一构建底座**：PanguEngine 只维护 CMake 4.0+ + Ninja 构建。普通 configure、编译、归档、最终链接和 Tests 不运行 Pangu 自制 AbiProfile，不保存逐 TU 收据，不由 Pangu 重新判断哪些 TU 应重编。
2. **同树源码构建豁免**：同一受控构建树、同一配置产生的 Pangu STATIC/SHARED Module、普通 EXE、测试 EXE 与 fixture 不生成、读取或比较 AbiProfile。STATIC Catalog 继续负责显式注册可达性，但最终链接成功不再触发额外的 Profile 收集或逐对象证明。
3. **配套插件豁免**：与宿主同树、同配置构建并随宿主整套复制、安装或交付的原生插件免 AbiProfile 预检；豁免跟随配套交付关系，不跟随目录。手工把另一棵树的 DLL 混入受控目录属于不支持用法，不为侦测这种操作恢复哈希、收据或逐次扫描。
4. **独立交付边界**：独立分发的 C++ SDK/预编译库，以及单独交付给已有宿主的原生 C++ 插件，属于二进制接纳边界。未来的兼容描述按目标平台、架构和有效 ABI 配置生成，不按 TU 保存对象来源证据。
5. **独立插件装载前预检保留**：对单独交付的原生插件，在 OS Load 和首次 C++ Query 之前仍必须非执行地确认实际待加载产物、目标 ABI、协议及 Public Module API Contract；无法可信绑定产物、解决检查后替换风险或确认兼容时 Fail Closed。装载后的 C++ Query 只做二次复核。
6. **旧 v1 协议退役**：ADR-0009 的 Schema、RFC 8785 Canonical、SHA-256 Fingerprint、strict-v1 比较与测试向量作为历史协议资料保留，但不再是普通生产构建或 Tests 的活动协议。不得把未来配置描述冒充为 strict-v1，也不迁移旧收据。
7. **Tests 与普通工具不参与 ABI 闸门**：Tests 可直接链接与生产代码相同的引擎库；测试数量增加不得导致共享依赖重复 ABI 扫描。第三方源码和 header-only C++ 库通过 `ThirdParty::` Target 表达自身要求，不强制稳定 C ABI 或 Pangu Module 包装。
8. **职责边界**：代码生成、资产处理和打包仅由实际消费者或显式命令触发；不通过 `ALL`、通用 `POST_BUILD` 或 ABI launcher 挂到普通 C++ 构建。不恢复 PanguBuild、新 Module 描述语言或 dev/verify 双轨。
9. **迁移顺序**：先接受本 ADR，再删除旧 AbiProfile 生产接线和无人使用实现。ADR 接受不等于实现已迁移；迁移完成前，仓库源码仍可能包含已被本 ADR 废止的旧机制。

## Consequences

- 日常增量构建回到 CMake/Ninja 原生依赖图，验证器实现变化不会单独强制业务对象重编。
- 不再证明每个本地对象的来源或侦测对受控 BuildRoot 的手工篡改；这是为降低构建复杂度与成本接受的边界。
- 同树/配套交付插件与独立交付插件必须在文档、打包和未来 Loader 中明确区分，不能仅凭文件路径推断。
- 独立 SDK/插件的下一版兼容描述、可信产物绑定与 Loader 实现另行设计；没有真实消费者时不预建 v2 工具。
- ADR-0010 的统一 C++23 Query/Registration、ModuleCodePin、回滚与生命周期语义保持；仅其“所有 SHARED 必须按 v1 预检”和 STATIC 最终 Profile 校验的适用范围被本 ADR 修订。

## Open Questions

独立交付二进制的最小配置格式、产物绑定与 TOCTOU 技术、Public Module API Contract/Digest 表示、SDK/Plugin Manifest 和发布流程，待出现真实导出方与消费方后单独冻结。
