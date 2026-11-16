# MinimalHost 归入 EngineHost 测试

状态：已实现；Windows 定向构建/运行与三桌面构建脚本回归通过。用户要求取消 Samples 构建开关，沿用此前将 MinimalHost 作为测试的讨论。

## 方案

将 `Samples/MinimalHost/` 移到 `Tests/EngineHost/Fixtures/MinimalHost/`，保留程序、服务、
目标名称、模块身份和实际断言。由 EngineHost 测试子树组织，MinimalHostSmoke 保留
system / engine-host / precommit 分类，累计进入 full。Engine 测试关闭时不创建这些目标。

移除根 Samples 选项与脚本的 `samples` 操作，不增加替代开关或操作；按需使用现有
`build -Target PanguMinimalHost` 或 Bash 的 `build --target PanguMinimalHost`。
测试脚本验证不再传入已删除选项，基准隔离测试验证测试开关控制 MinimalHost 的存在。

MyGame 的 ServiceHosts 保持 Engine 测试关闭，仅构建 Game/Editor。最小宿主断言由正式
MinimalHostSmoke 承接；嵌入 Engine 测试的 EngineOptIn 和 Monolithic ModuleRegression
必须继续构建与发现它，不削弱原覆盖。

迁移不改 C++ 行为或引入独立 Project；现行文档更新路径，历史验收保留当时命令并注明替代。
验证脚本、双链接模式的宿主运行、测试关闭隔离及必要的嵌入式 Project 接线。
