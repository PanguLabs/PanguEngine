# Module 正式测试

对应契约为 [Module API v1](../../wiki/core/module-api.md)，维护遵循 [Tests 规范](../README.md)。这些测试覆盖尚未冻结的 C++ API 实现，不代表 Plugin Public ABI 或完整生命周期已验收。

| CTest | 验证对象与失败判据 |
| --- | --- |
| `ModuleIdentity` | 固定身份文本/字节往返、非法身份拒绝与精确三段版本 |
| `StaticModuleHost` | 描述数据深复制、入口/身份/限制校验、Required/Optional 显式绑定、依赖图环与依赖优先顺序 |
| `ModuleRegistration` | 一次注册、声明暂存与原子发布、失败无半发布、粘性错误、报告进度、具体 Scope 隔离和重入拒绝 |
| `StaticModuleCatalog` | 两个真实受管 STATIC 夹具、最终 EXE 的生成根引用、链接裁剪后的入口可达与实际注册 |
| `ScopeAccess` | 原子闸门获取/关闭、借用释放、仅移动工厂与稳定控制块 |
| `ScopeBuild` | 实例先追踪后 Start、匹配销毁、类型核验与粘性错误 |
| `ScopeLifecycle` | 多 Scope 局部失败、激活/卸载/Shutdown、Busy 排空、隐式 Module 与显式资源依赖顺序、错误/进度、句柄代际及重入 |
| `ScopeDestructor` | 子进程确认错误析构 Busy Host 始终终止，未提前 Destroy 被借用对象 |

桌面宿主使用根 Preset 的正式 BuildRoot。例如 Windows：

```sh
cmake --preset windows
cmake --build --preset windows
ctest --preset windows -R '^(ModuleIdentity|StaticModuleHost|ModuleRegistration|StaticModuleCatalog|ScopeAccess|ScopeBuild|ScopeLifecycle|ScopeDestructor)$' --output-on-failure
```

Linux/macOS 替换为各自的 `linux`/`macos` Preset，源码仍读取约定共享挂载。夹具构建输出、生成 Catalog 与 ABI 记录在宿主 BuildRoot 中，不修改共享测试源码，不依赖 `.probe/`。

`Fixtures/First.cpp` 和 `Second.cpp` 各对应一个真实 Module STATIC Target，分别经过现有 ABI Guard/Compile/Collect。最终 EXE 开启平台链接裁剪，只经生成 Catalog 直接引用 Query；不用全局业务构造或 whole-archive。Catalog 输入故意为依赖方在前，注册顺序必须为 Provider 在前。

当前已新增拥有型工厂与实际 Scope 资源，使用资源生命周期适配而非统一业务 Service 基类。两个真实 STATIC 夹具分别提供 Host 范围非平凡 vector 对象和 Context 消费者，装配期执行跨归档 `Dependency<T>` 核验。标准拥有型容器 OOM 使用无异常终止政策；管理调用需外部串行化，已导出访问令牌支持并发关闭。声明注册域须比 Host 活得更久，回调不得移动/销毁 Host 或保留借用 Context。Shutdown 撤销本 Host 声明；撤销同样使原 Declarations 视图失效。

Host 退出前显式推进 Shutdown；若仍有工作或 Borrow，保持 Host 存活并重试。析构只允许安全完成，否则使用始终生效的终止路径。子进程测试安装确定的 terminate handler，以 ready 标记、退出码和无 Destroy 标记联合验证，不能将任意崩溃都视为通过。

Android/iOS 正式 Preset 默认不构建这些可运行测试，交叉编译只能验证 Runtime 库的目标编译/归档及宿主 ABI 采集。STATIC 夹具成功链接不构成最终 EXE ABI 一致性验收；SHARED 预检、TOCTOU、真实动态 CodePin、完整业务 Service/TaskScheduler/GPU 排空及动态卸载仍需后续测试。
