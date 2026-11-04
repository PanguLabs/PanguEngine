# ADR-0013：原生增量构建与独立二进制 ABI 交付边界

> Status: Accepted（原生 CMake/Ninja 增量构建边界；独立 Pangu Plugin/SDK 交付路线由 [ADR-0014](0014-source-plugins-project-build.md) 替代）
> 日期：2026-11-02
> 替代：[ADR-0009 ABI Profile / Fingerprint v1](0009-abi-profile-fingerprint.md) 的生产适用范围，并部分修订 [ADR-0010](0010-unified-cpp-module-entry.md)；本 ADR 的独立 Pangu 交付范围后续被 [ADR-0014](0014-source-plugins-project-build.md) 替代
> 设计依据：[原生增量构建与 ABI 交付边界](../../docs/superpowers/specs/2026-11-02-native-build-abi-boundary-design.md)

## Context

旧 ABI v1 将逐 TU 证据、STATIC 最终组合校验和 SHARED 预检接入普通构建。实践中它重复扫描同一目标、生成大体积收据，并使验证器变化反向失效已有对象，显著增加 configure、编译和链接成本。PanguEngine 已使用 CMake + Ninja；源码和链接依赖应由真实 CMake Target 描述，增量失效由 Ninja 与编译器依赖信息负责，而不是由第二套收据系统重新判断。

同时，独立交付的 C++ 二进制仍可能与宿主由不同构建产生，不能因为都使用 Clang/C++23 就默认 ABI 或 Module API 可安全互操作。因此需要把“本地增量构建”和“独立二进制接纳”分成两个职责。

## Decision

1. **单一构建底座**：PanguEngine 只维护 CMake 4.0+ + Ninja 构建。普通 configure、编译、归档、最终链接和 Tests 不运行 Pangu 自制 AbiProfile，不保存逐 TU 收据，不由 Pangu 重新判断哪些 TU 应重编。
2. **同树源码构建豁免**：同一受控构建树、同一配置产生的 Pangu STATIC/SHARED Module、普通 EXE、测试 EXE 与 fixture 不生成、读取或比较 AbiProfile。STATIC Catalog 继续负责显式注册可达性，但最终链接成功不再触发额外的 Profile 收集或逐对象证明。
3. **配套插件豁免**：与宿主同树、同配置构建并随宿主整套复制、安装或交付的原生插件免 AbiProfile 预检；豁免跟随配套交付关系，不跟随目录。手工把另一棵树的 DLL 混入受控目录属于不支持用法，不为侦测这种操作恢复哈希、收据或逐次扫描。
4. **独立交付边界（适用范围修订）**：原决策把独立 Pangu C++ SDK/原生插件与普通预编译 SDK 并列，准备未来以 ABI 配置实现接纳。**ADR-0014 已停止独立 Pangu Plugin/SDK 二进制交付路线**；系统 SDK 与普通 ThirdParty 预编译库仍按 Toolchain、供应商及自身 ABI/Ownership 契约独立审查，不因此需要统一 Pangu Preflight。
5. **独立 Pangu 插件装载前 Preflight（已终止）**：原决策要求在 OS Load 和首次 C++ Query 之前验证独立交付的 Pangu 原生插件及其可信产物绑定。根据 ADR-0014，此产品路线不再实施；同树 SHARED Module 仍须执行真实入口、ModuleId、依赖、Registration、CodePin 与安全卸载检查。
6. **旧 v1 协议退役**：ADR-0009 的 Schema、RFC 8785 Canonical、SHA-256 Fingerprint、strict-v1 比较与测试向量作为历史协议资料保留，但不再是普通生产构建或 Tests 的活动协议。不得把未来配置描述冒充为 strict-v1，也不迁移旧收据。
7. **Tests 与普通工具不参与 ABI 闸门**：Tests 可直接链接与生产代码相同的引擎库；测试数量增加不得导致共享依赖重复 ABI 扫描。第三方源码和 header-only C++ 库通过 `ThirdParty::` Target 表达自身要求，不强制稳定 C ABI 或 Pangu Module 包装。
8. **职责边界**：代码生成、资产处理和打包仅由实际消费者或显式命令触发；不通过 `ALL`、通用 `POST_BUILD` 或 ABI launcher 挂到普通 C++ 构建。不恢复 PanguBuild、新 Module 描述语言或 dev/verify 双轨。
9. **迁移顺序**：先接受本 ADR，再删除旧 AbiProfile 生产接线和无人使用实现。ADR 接受不等于实现已迁移；迁移完成前，仓库源码仍可能包含已被本 ADR 废止的旧机制。

## Consequences

- 日常增量构建回到 CMake/Ninja 原生依赖图，验证器实现变化不会单独强制业务对象重编。
- 不再证明每个本地对象的来源或侦测对受控 BuildRoot 的手工篡改；这是为降低构建复杂度与成本接受的边界。
- 当前 Pangu 原生 Plugin 只有源码同图编译的交付路径；不能仅凭某个 DLL 文件路径推定其已被授权加载或 ABI 兼容。
- 独立 Pangu SDK/Plugin 的 config-v2、可信产物绑定和 Preflight 不再设计或实现；运行时 Loader 的入口/生命周期检查与实际部署仍需要另行验收。
- ADR-0010 的统一 C++23 Query/Registration、ModuleCodePin、回滚与生命周期语义保持；其历史 Profile 校验由本 ADR 取消，独立 Pangu Plugin Preflight 路线进一步由 ADR-0014 替代。

## Open Questions

独立 Pangu Plugin/SDK 的 config-v2、产物绑定与 TOCTOU 技术已从当前产品路线退出。普通 ThirdParty/系统 SDK 仍需按其真实 ABI/Ownership 契约审查；Pangu SHARED Loader 的运行时检查与完整部署另行实施。
