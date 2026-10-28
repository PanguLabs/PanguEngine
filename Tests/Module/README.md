# Module 正式测试

对应契约为 [Module API v1](../../wiki/core/module-api.md)，维护遵循 [Tests 规范](../README.md)。这些测试覆盖尚未冻结的 C++ API 实现，不代表 Plugin Public ABI 或完整生命周期已验收。

| CTest | 验证对象与失败判据 |
| --- | --- |
| `ModuleIdentity` | 固定身份文本/字节往返、非法身份拒绝与精确三段版本 |
| `StaticModuleHost` | 描述数据深复制、入口/身份/限制校验、Required/Optional 显式绑定、依赖图环与依赖优先顺序 |
| `ModuleRegistration` | 一次注册、声明暂存与原子发布、失败无半发布、粘性错误、报告进度、具体 Scope 隔离和重入拒绝 |
| `StaticModuleCatalog` | 两个真实受管 STATIC 夹具、最终 EXE 的生成根引用、链接裁剪后的入口可达与实际注册 |

桌面宿主使用根 Preset 的正式 BuildRoot。例如 Windows：

```sh
cmake --preset windows
cmake --build --preset windows
ctest --preset windows -R '^(ModuleIdentity|StaticModuleHost|ModuleRegistration|StaticModuleCatalog)$' --output-on-failure
```

Linux/macOS 替换为各自的 `linux`/`macos` Preset，源码仍读取约定共享挂载。夹具构建输出、生成 Catalog 与 ABI 记录在宿主 BuildRoot 中，不修改共享测试源码，不依赖 `.probe/`。

`Fixtures/First.cpp` 和 `Second.cpp` 各对应一个真实 Module STATIC Target，分别经过现有 ABI Guard/Compile/Collect。最终 EXE 开启平台链接裁剪，只经生成 Catalog 直接引用 Query；不用全局业务构造或 whole-archive。Catalog 输入故意为依赖方在前，注册顺序必须为 Provider 在前。

当前仅保存强类型声明身份、Owner 和诊断名；未创建 Service 实例、任务或回调资源。标准拥有型容器 OOM 使用无异常终止政策。调用须外部串行化，Scope 生命周期长于 Host，回调不得移动/销毁 Host 或保留 RegistrationContext。不同 Scope 的嵌套操作允许，同一 Scope 的跨 Host 重入拒绝。

Android/iOS 正式 Preset 默认不构建这些可运行测试，交叉编译只能验证 Runtime 库的目标编译/归档及宿主 ABI 采集。STATIC 夹具成功链接不构成最终 EXE ABI 一致性验收；SHARED 预检、TOCTOU、CodePin、激活、Service/工作排空及动态卸载仍需后续测试。
