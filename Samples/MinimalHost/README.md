# 最小服务宿主

Application 拥有 `EngineHost`，显式 Start、创建两个 Context 及各自 World，再通过业务头内 `AcquireCounter` 取得服务。`CounterService` 是没有共同基类或虚函数的普通类型；Factory 固定创建初值 7，不提供通用构造参数注入。程序检查第一个服务改为 8 时另一个仍为 7，释放 Borrow 后关闭第一个 Context，最后显式关闭整个宿主。

```powershell
cmake --preset windows -DPANGU_BUILD_SAMPLES=ON
cmake --build --preset windows --target PanguMinimalHost
ctest --preset windows -R '^MinimalHostSmoke$' --no-tests=error --output-on-failure
```

默认关闭 `PANGU_BUILD_SAMPLES`。Monolithic 直接运行 `PanguMinimalHost`，静态 Query 是显式链接根；Modular 运行 `PanguMinimalHost <PanguMinimalServices 动态库的绝对路径>`，程序不搜索目录，主程序不隐式导入业务库。Linux Modular 主程序导出 RTTI，公开服务类型使用 default visibility。

本例只有同步结束的资源，因此一次 Finish 应完成；失败会报告诊断并返回非零，析构兜底若仍无法关闭将按宿主契约终止。真实应用的异步任务、Fence 和 I/O 等待由 Application 协调，在完成事件到达后再次推进关闭；不能照抄无限空转或强制卸载。此示例不提供后台清理线程。
