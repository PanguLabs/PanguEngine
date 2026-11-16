# PanguEngine 的 find_package 接入

状态：已实现；Windows 定向验证通过，其他平台与全量矩阵未在本次重跑。用户要求参考 O3DE，以 FindPanguEngine 接入并同步现有消费者。

## 接口与边界

独立 Project 将选定 Engine 的 `cmake/` 加入 `CMAKE_MODULE_PATH`，调用
`find_package(PanguEngine REQUIRED MODULE)`，再于 `project()` 后调用
`pangu_initialize()`。查找只确认源码树并提供初始化函数，不编译工具、不创建 Runtime 目标。
初始化沿用单个 CMake/Ninja 图和 Engine/Project 产物分根；
`PANGU_ENGINE_BUILD_ROOT` 未指定时沿用顶层 binary root，指定时必须为绝对路径。
`PANGU_ENGINE_SOURCE_DIR` 继续公开；已有显式值必须与所选 Find 模块所在 Engine 一致。

Project 仍拥有工具链、CTest、版本锁及插件启用策略。保留 `Engine.commit` 校验，
不在查找时隐式开启 Engine 内部测试。源码 Plugin 政策和 Monolithic/Modular 行为不变。

## 方案取舍

- 采用 O3DE 的查找、初始化两步入口，避免查找包时运行完整配置或更改父工程。
- 未采用查找时自动添加 Engine：这会给普通包探测引入构建副作用。
- 不增加全局注册表、已安装 SDK 或二进制分发；这些超出当前源码集成要求。

上述实现细节由本次实施选择。旧 `PanguEngine.cmake` / `pangu_add_engine()` 入口统一替换。
完整引入 Engine 的 MyGame、Meta Project、链接模式、Log 配置和 Shared 模块夹具同步迁移。
Shared 独立入口迁至 `Tests/Module/Fixtures/SharedProject/`；内部目标保留在 `Shared/`，
避免顶层与 Engine 内部重复加入同一源码目录导致属性遍历递归。
单独引入 Foundation 或少数 Runtime 模块的隔离测试保持其现有测试边界。
Engine 自己的 Samples 与内部 Targets 继续由 Engine 根工程组织。

## 验证

增加无编译器依赖的包发现测试：发现成功、重复查找无构建副作用、QUIET 缺失、
REQUIRED 缺失、源码路径冲突及初始化拒绝相对 BuildRoot。
复用真实集成测试验证初始化后的目标、链接模式、宿主运行及产物归属。
`ModuleSharedProject` 配置独立入口，构建并运行 13 个原生模块测试，防止源码目录重复纳入导致递归。
各平台执行范围单独记录，不将 Windows 验证外推为其他平台通过。
