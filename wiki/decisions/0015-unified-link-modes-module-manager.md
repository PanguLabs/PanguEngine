# ADR-0015：统一链接模式与 ModuleManager

> Status: Accepted（设计与行为；Public C++ API/ABI 未冻结）
> 日期：2026-11-06
> 权威规范：[Module API](../core/module-api.md)、[Build System](../foundation/build-system.md)、[Module 生命周期](../core/module-registration-lifetime.md)

## Context

原规则允许逐 Module 混合 STATIC/SHARED，并由一个 SHARED Query 交付内嵌 STATIC Catalog；多逻辑记录共同承载使接纳、依赖和真实卸载协调复杂。旧 StaticModuleHost 仅覆盖同二进制 Scope，无法作为运行中真实库加载、增量 Scope 和卸载后访问凭据的完整协议。已批准[设计规格](../../docs/superpowers/specs/2026-11-06-module-manager-link-modes-design.md)选择统一构建模式和一个协调器，沿用受控 C++ ABI 与所有权契约。

## Decision

1. 同一目标图所有 Pangu Module 用 Monolithic/Modular 统一为 STATIC/SHARED。桌面默认 Modular，移动默认 Monolithic；父 Project 控制 Engine 子目录。保留 ADR-0005 的唯一 Target/主要库产物，替代其静态逻辑 Module 嵌入可卸载 SHARED 的允许范围；普通 ThirdParty 不受此限制。
2. 替代 ADR-0011 Decision 2/3 的同库 Catalog/多记录共同承载规则。Monolithic Catalog 归最终 Host，Modular 单库单 Query 单 Module；删除 LinkedStaticModules，不建 BinaryImage 或同 Module 双变体。OS 额外引用仍可能保持库驻留，成功只保证 Manager 自己的释放调用和受管收尾完成。
3. 直接用 ModuleManager 替代 StaticModuleHost，批量显式 Load 只保证本批受管接纳，不包括注册/激活和 DLL 初始化副作用。稳定记录、代际句柄、原生持有与失败清理统一协调。
4. Foundation/Module 具有持久身份，通过宿主已链接 Query 常驻登记，仍显式 Register/Activate，普通卸载拒绝。对 ADR-0011 Decision 6 的可卸载失败收尾增加常驻例外：执行失败同次请求整体 Shutdown，原错和诊断句柄跨 Poll 保留，TryFinishShutdown 成功统一失效，不释放宿主原生持有，不原地重启。前置拒绝不请求 Shutdown。
5. 强类型接口在内部擦除处比较 RTTI 值，不采用 RTTI 地址或名字/hash、人工 TypeId/trait。访问/Pin 控制块和最终释放由常驻 Runtime 提供；业务 RTTI/Factory/Deleter 在卸载前解除，残留 ScopeAccess 不保活业务对象。
6. 整体 Shutdown 按实际有效资源 DAG、同 Module 后代资源顺序推进，依赖方原生释放未确认前保留 Provider。替代旧 Scope 规格“先完成全部 Scope 再退出 Module”的整体顺序；独立 Scope 关闭仍子先父且父有子记录不能完成。常驻可先排空资源/保护并解除依赖边，最后统一确认，避免常驻 Consumer 与业务 Provider 自锁。

## Considered Options

保留混合类型/同库账本会维持多逻辑身份与镜像的耦合；统一模式使每库恰好一项受管身份。新建通用 Loader/BinaryImage 层会重复管理职责，选择协调器内部平台接缝。人工 TypeId 要业务维护额外身份协议，选择受控 ABI 下的 RTTI 值比较与真实跨库验证；持久 Schema 身份留给其领域。

## Consequences

POSIX 配套 Module 使用 RTLD_NOW|RTLD_GLOBAL，替代最初 Task 3 的 LOCAL 实现选择。Linux/macOS 两个只有逻辑依赖、无原生互导的真实 Scope 模块在 LOCAL 下合法 RTTI 值匹配失败；同实际编译配置的两独立 DSO 对照为 LOCAL 值比较0、GLOBAL为1。比较仍用 std::type_info，不引入人工类型身份、逐类型导出锚点或名字/hash 比较。GLOBAL 使公共符号参与进程作用域重定位，有名称冲突/符号插入代价；继续保持最小公共导出、私有隐藏和受控配置/ODR，不宣称插件隔离。[libc++ 可见性](https://libcxx.llvm.org/DesignDocs/VisibilityMacros.html)说明 RTTI 导出属于 ABI；[POSIX dlopen](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dlopen.html)规定 GLOBAL 的重定位范围。平台探针是当前工具链证据，不是所有工具链的普遍保证。

自身原生释放成功与物理移除分开：[POSIX dlclose](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dlclose.html)不要求成功即物理移除，仍须保留既有重定位引用。特定 Pin/关闭故障用例要观测 Consumer 物理卸载早于 Provider Destroy，须先显式装载 Provider，排除 Consumer 先隐式装入 Provider 形成的组保留。两输入顺序、自身 wrapper 失效、最终实际卸载与额外 OS 持有分别由正式 NativeLoader 回归保留，Manager 不等待未承诺的物理移除，也不强制卸载。

保留身份/版本、显式依赖、注册事务、Scope 局部回滚、Borrow/Allocator/CodePin 和禁止强卸载的既有契约。失败批次依赖未知且原生释放失败时，私有有界组保守保护潜在 Provider，可能暂时阻止无关卸载；释放全部完成必须解除。仅未接纳未注册的拒绝批次可对非法元数据环逐项释放自身 OS 持有，不扩展为有效业务图的清理规则。

公共导出、各桌面真实 RTTI/装卸、卸载后凭据、两移动平台最终链接和六场景 benchmark 必须实际验收；常驻根成本留在 Create/析构计时内，不直接混用旧基线。实现与实际证据见[验收记录](../../docs/superpowers/plans/2026-11-06-module-manager-link-modes-validation.md)。历史计划/规格保留当时事实并加替代链接。

## Open Questions

Public C++ 布局/长期 ABI、完整 EngineHost/ServiceRegistry、Meta 生成协议、Hot Reload、安装分发和移动设备动态加载政策仍未冻结。Android/iOS 编译与最终链接不代表设备运行、打包或签名。
