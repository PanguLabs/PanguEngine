# ADR-0011：冻结 Module API v1 的行为契约

> Status: Accepted（仅行为契约；Public API/ABI 与生产 Loader 待验证）
> 日期：2026-10-22
> 权威规范：[ModuleDescriptor & Registration API v1](../core/module-api.md)
> 基线：[ADR-0007](0007-error-diagnostics.md)、[ADR-0008](0008-module-registration-lifetime.md)、[ADR-0010](0010-unified-cpp-module-entry.md)

## Context

统一 C++ Module 发现/注册路线已确认，但 API 草案尚有身份、依赖绑定、失败进度、后续 Scope 装配与同库代码驻留等行为分歧。仅凭原始 Error 无法判断外层操作是否已经产生副作用；每次新增 World 重跑 Module 注册也会混淆全局能力与局部实例。STATIC 嵌入 SHARED 时，唯一 Query 还必须使内嵌 Catalog 可达。

本轮明确先冻结可执行的行为契约，保留具体 Public Header、类型布局、ABI 与生产 Loader 的验证门槛。局部 C++ 探针用于检验设计，不充当完整引擎验收。

## Decision

1. 身份表示与版本规则、依赖选择及阶段要求按权威规范第 2 节冻结；不再把身份位宽与 v1 精确版本匹配列为未决问题。C++ 布局和生成工具仍未冻结。
2. 静态 Catalog 必须从最终承载目标交付。SHARED 的单一 C++ Query 使其自身及内嵌 STATIC 定义可达；各逻辑 Module 独立记录，CodePin 保护实际承载库，不增加架构层。
3. **澄清 ADR-0008 的 Unloaded 终态**：逻辑 Module 清理结束并释放其承载持有，不等于同库代码必然已被 OS 移除。实际库仍被其它记录或保护使用时须保留；最后持有的实际卸载失败不能伪装成成功。替换产物须完成旧承载库的全部释放，不引入多代际并存。
4. 每次 Load 生命周期至多执行一次注册回调，后续 Scope 使用可重复装配声明。新增 Scope 的失败仅回滚本次装配，不能隐式停用已有 Scope；Module 停用与新装配发布按同一可验证规则裁决。
5. 运行工作由具体 Scope Owner 在启动前追踪，通过显式停止、完成检查与依赖逆序销毁参与收尾；不强制统一生命周期基类。
6. 操作后置条件和继续清理责任按权威规范第 6.1 节冻结。原始 Error、不可丢失的控制流进度和可截断的诊断报告分离。注册/首次激活执行失败后统一收尾再重新 Load；未交付句柄的装载失败由 Manager 接管清理。
7. 以上细化保持 ADR-0007 的错误/报告分工、ADR-0008 的事务/生命周期边界和 ADR-0010 的统一 C++ 入口及装载前预检要求；精确 API/ABI 不因行为冻结而自动接受。

## Alternatives

- 从 Error Domain/Code 推断操作阶段：无法区分回调传播同一 Error 的场景，采用独立进度信息。
- 每个 Scope 重跑 Module 注册或复用一个可移动实例：会重复全局注册或混用可变状态，采用一次声明、多次局部装配。
- 等同库所有逻辑 Module 停止后才能完成任一逻辑退出：混淆依赖清理进展与物理库寿命，采用独立记录和共同承载保护。
- 同时冻结公共头文件及二进制协议：Service/Error 等依赖类型和跨平台验证尚未完成，本轮只冻结行为。

## Consequences

实现可以依据确定的状态、依赖、回滚与所有权规则推进；方法签名、Factory/Scope/Progress/Report 类型、生成器、元数据预算与生产 Loader 工艺仍需专项设计及验收。

本地 Windows 探针通过 9 项 CTest，并检查单 Query 导出；具体覆盖和限制统一记录在[权威规范第 9 节](../core/module-api.md#9-本轮验证证据与限制)，不据此宣称生产 ABI 或跨平台加载已通过。局部探针不纳入正式测试目录。

## Open Questions

精确 Public API/ABI、平台导出约定、可信产物绑定/TOCTOU、数据上限与无异常分配策略、Scope/任务调度集成、并发实现、完整跨平台回归。身份和本轮列出的行为规则已冻结，其修改需要显式后续决策。
