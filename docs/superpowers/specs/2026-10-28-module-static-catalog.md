# Module API v1：Descriptor、STATIC Catalog 与声明事务

> 状态：实施规格；用户已批准本轮范围并要求在新分支实施。具体 Public API/ABI 仍为未冻结的实现切片。

## 目标与依据

将 `wiki/core/module-api.md` 第 7 节的前三个切片接入正式 Runtime 和 Tests：宿主复制描述数据、校验身份及显式依赖；通过最终目标生成的 Catalog 根引用真实 STATIC Module；注册回调提交声明，同一具体 Scope 内全部成功后发布，失败无半发布且保留原始 Error。

## 方案与范围

采用现有 `PanguModule` 内的 `StaticModuleHost`，不增加第二个 Module 库。相较一次实现完整 ModuleManager/Service/Loader，此方案先验证身份、依赖、所有权与事务边界；相较继续维护临时 PoC，它提供版本化源码、真实归档链接及 CTest 回归。这是实现选择，不新增架构冻结。

- `ModuleDescriptor` 借用身份、版本、协议版本、依赖与诊断字符串；Host 复制为独立拥有的数据。版本 `0.0.0` 合法，三分量精确匹配。
- `StaticModuleHost::Create` 接受显式 Catalog、Optional 绑定列表和具体 `RegistrationScope&`，全部验证成功才返回 Host。Query 恰好查询一次；空 Query/定义/注册入口、嵌套 Catalog、零 ID、重复身份/依赖、自依赖、非法协议/依赖种类及有效图中的环均拒绝。
- Required 必须满足；Optional 仅由 `(依赖方, 被依赖方)` 显式选中，未绑定记录区分缺失、版本不符、未选择。绑定成功后固定，注册要求 Provider 已 Registered。
- `RegistrationContext::Declare` 暂存强类型 `DeclarationId` 与诊断名；它是声明元数据的最小事务接口，不是 Service Provider/Factory API。Scope 拥有最终声明，成功时一次发布；冲突或回调失败销毁暂存内容，既有声明保持不变。声明失败为粘性错误，回调忽略失败也不能提交。
- 每个 Host 的每个 Module 只允许一次成功注册；回调执行失败后只保留失败状态，不允许重试。前置拒绝不改变状态。操作报告用值类型保存主错误、涉及身份和进度，不借用调用方以供异步使用。
- 所有操作由调用方串行执行；Scope 内注册期间拒绝重入，包括另一个 Host。无工作、任务、资源及 Service 实例启动，因此本切片回滚同步完成。
- Host 和 Scope 均不可复制；Scope 不可移动，且须比借用它的 Host 活得更久。Host 可移动；元数据快照与 Scope 声明在源数据销毁后仍有效。
- 冷路径使用标准拥有型容器，OOM 沿用禁用异常下的终止政策，不承诺可恢复 OOM。上限：256 个 Module，每个 64 条依赖，诊断名/BuildIdentity 各 256 字节；超过上限在复制前拒绝。
- 单次注册最多 256 条声明，同一具体 Scope 最多 4096 条声明；越界属于粘性失败，不发布本次已暂存的内容。

Catalog 由最终链接目标生成装配 `.cpp`，直接引用各 Query，不使用全局业务构造或 whole-archive。测试建立两个真实受管 STATIC Module，并开启链接裁剪，验证逆序输入仍按依赖顺序注册。

## ABI 与后续边界

本轮是受控同次构建内的 STATIC 启动/声明 API，不提供外部候选接入、SHARED Query、OS Load、CodePin、Activate 或受管卸载。Descriptor 暂不提供可手填的 ABI Profile/Fingerprint 字段。现有构建仍采集受管 STATIC 库的真实 ABI；最终 EXE 的完整 ABI 校验、Profile 嵌入和 Public API Contract 需后续切片，不能将本次链接夹具视为最终消费者 ABI 安全验收，也不能将本 API 用作生产 Loader 的放行路径。

## 验收

正式测试覆盖源元数据修改/销毁后的深复制；全部依赖和入口负测；Optional 显式选择及图环；第 N 条声明失败无半发布、原错误/进度与前置拒绝区别、不同具体 Scope 隔离、重入及重复注册拒绝。真实 STATIC 夹具确认裁剪后两个入口可达，错误回调不会发布声明。Windows 完整 CTest；Linux/macOS 尽可能执行正式 Preset 构建及定向测试；Android/iOS 执行 Runtime 编译/归档，明确设备运行未验证。针对新增 C++ 使用仓库 clang-format、真实编译数据库 clang-tidy。
