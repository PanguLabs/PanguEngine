# spdlog

来源为 [gabime/spdlog](https://github.com/gabime/spdlog)，固定 tag `v1.17.0`、提交
`79524ddd08a4ec981b7fea76afd08ee05f83755d`。`Repo/` 为 Git submodule，
许可证为 [MIT](Repo/LICENSE)。当前通过扩展点适配，没有上游补丁。

`ThirdParty::Spdlog` 是编译型静态库，使用上游标准六个编译单元，排除 bundled fmt，
只链接本树 `ThirdParty::Fmt`。设置 `SPDLOG_COMPILED_LIB`、`SPDLOG_FMT_EXTERNAL`、
`SPDLOG_NO_EXCEPTIONS`、`SPDLOG_NO_THREAD_ID`、`SPDLOG_DISABLE_DEFAULT_LOGGER`，
保留 RTTI、PIC，清空 `DEBUG_POSTFIX`。不调用上游构建配置，不建立 tests/examples/install，
不改全局 `BUILD_SHARED_LIBS`，不继承 `PanguBuildOptions` 的警告策略。
普通翻译单元关闭 C++ Modules 扫描，保留正常编译依赖；共享对象的配置回切与 no-op
由正式 ProjectSwitch 回归检查，不额外注入引擎上下文、诊断宏或 Module 身份。
编译库中包含上游 registry/格式化/异步 API，并不表示 Pangu 后端调用这些路径。

## 后端使用路径

Log 私有 `SpdlogBackend` 直接持有两个内联 `RawSink : spdlog::sinks::sink`，
消费者以显式 time/source、空 logger name 和已编码有界文本构造借用型
`spdlog::details::log_msg`，串行调用同步 Sink 的 `log()` / `flush()`。
这使用批准规格的同步 logger/Sink 入口；不实例化 `spdlog::logger`，
不构造它的 name、sinks vector 或 backtracer，也不创建 formatter、registry 或异步池。
等级映射、Off/非法等级不写入、原始括号/嵌入 NUL、两路独立结果均由正式回归验证。

`RawSink` 只借用 Context、Write/Flush 函数，返回每路独立 `OutputOutcome`。
它不打开文件、不分配资源、不拥有输出接缝。误调用 pattern/formatter 配置后，
该路返回 `InvalidConfig` 并停止转发。析构仅销毁内联值，不调用输出接缝。

当前必经上游路径的审计范围：

- `include/spdlog/details/log_msg-inl.h`：显式时间绕过时钟获取；禁用 thread ID 后
  只写入值和视图，空 name 使用 `string_view_t`，不构造 `std::string`。
- `include/spdlog/sinks/sink.h`：仅等级 atomic 与虚接口，没有 formatter 或容器存储。
- 同步派发直接调用两个 `RawSink`，不经过 logger 的 backtracer、错误处理器或 registry。

历史实现的 `BackendLogger : spdlog::logger` 只在 Release 的 iterator-debug=0
路径完成过初始审计；Windows Debug 的空 string/vector 仍创建 iterator proxy，
不能满足无异常可恢复初始化。2026-11-13 的修复删除这一私有类型，
保持正常 STL Debug ABI=2 和上游源码/编译配置不变，未添加第三方补丁。
## 正式关卡及证据边界

从根目录先构建 `PanguLog PanguLogBackendTests PanguLogNoAllocationFixture`，再运行：

```sh
ctest --preset windows -R '^PanguLogBackendTests[.]Backend[.]|^LogNoAllocation$' --no-tests=error --output-on-failure
```

Linux/macOS 使用根 `linux`/`macos` preset 和 AGENTS.md 指定的共享源码路径及隔离 BuildRoot。
2026-11-12 的三桌面 Release/Modular 结果属于历史 BackendLogger 路径。2026-11-13 的新路径与平台结果分别记录在正式验收，不把历史数量追认为当前源码的完整矩阵。
测试验证原样保留括号和嵌入 NUL、输出失败隔离、无全局注册、误配置拒绝。
独立夹具在开启分配失败后构造空后端及两路后端、Emit、Flush、析构，要求正常退出、
`AllocationRequests=0 ConsoleWrites=1`；监督脚本先以故意分配子进程自验证探针。

| 平台 | new 捕获 | C 分配捕获与盲区 |
| --- | --- | --- |
| Windows | 测试进程 ordinary/array/nothrow/aligned new 替换 | Debug 独立 EXE 另以 CRT allocation hook 对真实 malloc 自验证并拒绝两个初始化 Try 分配；Release 不拦 CRT malloc/HeapAlloc，且不声称捕获全部 DLL/OS 内部入口 |
| WSL Linux | 同上 | 链接器 `--wrap` 捕获可执行文件/静态库对 malloc/calloc/realloc/aligned_alloc/posix_memalign 的直接引用，不覆盖 libc/libc++ 动态库内部绑定 |
| macOS | 同上 | 不拦截系统 malloc zone 或系统 dylib 内部 C 入口；以当前 libc++ 头文件的空构造源码补充 |

历史 BackendLogger 审计使用 MSVC STL 145（`_MSVC_STL_UPDATE=202604L`）、`_ITERATOR_DEBUG_LEVEL=0`：
`xstring::_Construct_empty` 使用 SSO，`vector::_Construct_n(0)` 不进入 `_Buy_nonzero`；
`xmemory` 的 `_Container_base0::_Alloc_proxy` 及 `_Fake_proxy_ptr_impl` 是 no-op；
`mutex::_Mutex_base` 默认分支仅初始化内联状态；空 `std::function` 不持有 callable。
这些是 CRT 未拦截构造路径的源码依据，不适用于启用 iterator debug 的其他配置。

历史空容器审计的 Linux 使用 LLVM libc++ 23.1.0，macOS 使用 MacPorts libc++ 23.1.2 头文件和 Apple 系统
`libc++.1.dylib`。两套头文件的 `string` 默认构造只初始化空表示，
`__vector/vector.h::__init_with_size` 仅在 `n>0` 分配，
`__functional/function.h` 的空/nullptr 构造无分配；`__mutex/mutex.h` 以
`PTHREAD_MUTEX_INITIALIZER` 初始化内联 mutex。Linux mutex 析构为 trivial，
macOS 的 out-of-line mutex 析构进入系统运行库；pthread 接缝见
`__thread/support/pthread.h::__libcpp_mutex_destroy`。系统运行库内部 C 分配并未被探针完整捕获。

因此结论限定为已审计调用路径及所选配置的实测与源码依据，不能泛化为整个 spdlog/fmt、
全部 CRT/OS 分配或任意标准库/Debug/Sanitizer 配置都已覆盖。生产目标不携带探针替换和故障开关。
