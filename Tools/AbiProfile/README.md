# PanguAbiProfileTool — ABI Profile v1 采集与编码工具（Build-Time）

> 实现状态：五平台受管 STATIC Module 的 ABI Collection 已完成（2026-10-27），Task 1—12 和追加评审修复均已独立复审通过。最近一次完整 CTest 为 Windows **36/36**、Linux **34/34**、macOS **34/34**，合计 **104/104**；Android/iOS ARM64 及 Windows Debug MD/Release MT 的构建、Host 采集和增量验证通过。详见[计划与验收状态](../../docs/superpowers/plans/2026-10-23-abi-collection.md)。追加修复与文档已按用户授权通过 amend 合入同一提交，未推送；最终 EXE/SHARED Loader 不在本轮范围。

本工具由 CMake 在当前宿主的独立 Host BuildRoot 编译并发布，通过 `cmake/HostTools.cmake` 获得可执行程序路径（目标平台构建目录不直接执行 Host 工具）。依赖固定于 `Source/ThirdParty/NlohmannJson/Repo` submodule 的 nlohmann/json v3.12.0 正式发布版，构建前需初始化 submodule；仅包含工具代码和测试，不构成新的 Pangu Runtime Module。

原有四参数编码模式保持兼容。输入可使用正式 C++ 采集生成的 `profile.json`；手工 JSON 仅用于算法诊断，不能冒充真实构建配置：

```sh
<HostToolPath>/PanguAbiProfileTool[.exe] \
  wiki/foundation/abi-profile-v1.schema.json \
  <validated-input-profile.json> \
  <BuildRoot>/AbiProfile.canonical.json \
  <BuildRoot>/AbiProfile.sha256
```

当前支持：

- 先检查**冻结 Schema 文件本身**的固定 SHA-256（只规范化 CRLF/LF 换行，不接受调用方替换为更宽松的同名 Schema），再解析 JSON、拒绝重复 Key，并校验 v1 Profile 字段；
- 在 v1 JSON 约束下按 RFC 8785 JCS 递归排序对象 Key，保留数组顺序；
- 计算 SHA-256(`PANGU_ABI_PROFILE_V1\0` + UTF-8 canonical JSON)，输出小写十六进制 64 字符，不添加换行；
- 不符合 Schema / malformed / duplicate-key 输入时返回非零状态，**不生成结果文件**；
- 使用 `Tests/AbiProfile` 的正式 CTest 检查冻结向量及 Windows/Linux/macOS 平台组合约束。

**四参数编码模式的边界**：仅核验声明的 Profile JSON 内容格式和 Digest，不独立证明声明来源。正式五平台 C++ 构建流程另外核验当前构建的编译收据、对象归档和依赖闭包；仍不能证明最终 EXE/SHARED Loader 的可信绑定、TOCTOU 防护或动态加载后的复核，不得据此放行生产 SHARED Loader。

此工具刻意作为 Host 原生程序而非 Android/iOS 目标程序构建运行；交叉编译情形由 Windows/macOS Host 工具读取目标编译产物并采集受控 Profile。

## 统一五平台 ABI Collection

生产 Root 已切换到统一 C++ Guard → Compiler Launcher → STATIC 归档 → Collect 管线。Windows 专用 `pangu_enable_windows_abi_profiles()`、独立 `<Target>AbiProfile` Target 和 `--collect-windows` 旧 CLI **不再使用**。

- 普通受管 STATIC Module 调用 `pangu_register_abi_module(Target)`；普通技术 C++ STATIC 库按需调用 `pangu_register_abi_static(Target)`。相关 Target 全部创建后调用 `pangu_finalize_abi_profiles(TARGETS ...)`。一个 Module 始终只生成一个原生库 Target。
- CMake 导出每配置的实际 Target 及链接事实，利用 `TRANSITIVE_LINK_PROPERTIES` 安排经 `INTERFACE`、条件 Generator Expression 选中的技术库先完成。依赖分类、严格传递闭包、ABI 比较、SCC 分组和组封口由 C++ `DependencyGraph` 与 `CollectionState` 负责，不在 CMake 中重新实现。
- 统一 CLI：`PanguAbiProfileTool guard --request <request.json>`、`compile --request <request.json> -- <compiler args...>`、`collect --request <request.json>`、`invalidate --request <request.json>`；另有内部 `guard-technical` / `collect-technical` 命令。原有四参数纯编码模式兼容，但不会发布可信构建完成记录。
- 所有受管 Target 的 ABI 输出统一为 `<BuildRoot>/Abi/<Config>/<Target>/`。CMake 生成 `request.json`、`objects.txt`、`inputs.h`，编译器 Launcher 发布 TU 实际编译收据；采集成功后发布 `evidence.json`、`profile.json`、`canonical.json`、`fingerprint.sha256`，最终发布 `complete.json`。技术 STATIC SCC 在组首成员目录额外发布 `group-complete.json`。
- Host 工具由 `cmake/HostTools.cmake` 在对应宿主环境单独构建与运行。Android 使用 Windows Host 工具，iOS 使用 macOS Host 工具；不在交叉目标上执行 Host 采集工具。
- 既有对象如果缺少完整且匹配的真实编译收据，Guard 会 Fail Closed。不要伪造或覆盖收据，需使用新的独立 BuildRoot 重新构建。
- 观测通过 VFS 将 Probe 附加到真实顶层 Source，保留 `__INCLUDE_LEVEL__`、`__BASE_FILE__`、相对包含及源文件时间语义；Windows 同时检查源/头内 STL ABI 控制和实际 COFF iterator/CRT 指令。
- 每次观测使用原子新建的私有 `work/.observation-*` 输出区，完成后清理；证据只规范化实际生成路径，真实 Header/运行库等输入保持原始路径及身份。目标观测锁不覆盖真实编译过程，多 TU 可以并行编译。
- 技术 STATIC 归档环可以经 INTERFACE 传播；只有实际 STATIC 成员参与组采集，INTERFACE 不生成虚构 Profile。包含受管 Module 的环仍拒绝。
- 注册前若目标已有非空 `CXX_COMPILER_LAUNCHER`（包括全局变量初始化的值），明确拒绝未知组合，不静默覆盖已有 wrapper。

例如使用 Windows Preset：

```sh
cmake --preset windows
cmake --build --preset windows --target PanguModule
ctest --preset windows --output-on-failure
```

Linux/macOS 分别使用本机 ext4/APFS BuildRoot（不是 SMB），Android/iOS 使用既定跨平台 Toolchain。Windows 的受限 MSVC STL/CRT、Linux 的受限 libc++/glibc，以及 Apple/Android 的 SDK 与运行库证据都由对应 C++ Collector 验证。最终消费者 EXE/SHARED Loader 的 ABI 一致性、TOCTOU 防护或动态加载后的可信身份 **不在当前 STATIC Profile 保证范围内**，不得将 Build-Time Guard 视为生产 Loader 认证。

## 失败报告与文件状态

原四参数编码命令继续兼容；统一 Guard / Compile / Collect / Invalidate 复用结构化失败报告。失败时以非零退出码表示结果分类，stdout 输出一行独立的 JSON 失败报告，
stderr 提供提示及失败探针的有界 JSON 诊断（`operation=abi-probe`）；正式 stdout 失败报告的可选 `probe` 字段也独立拥有首个失败探针的阶段、程序、工作目录、参数、实际退出码和捕获输出。参数最多保留32项、每项256字节，stdout/stderr分别最多8/32 KiB，截断时标记 `truncated=true`；后续失败不替换首现场。调用方即使不保存stderr，也可从操作报告取得必要诊断。

- 报告的 `schema=1`、`operation=abi-profile`；`errors[0]` 是主错误，后续错误保留清理失败等次要现场。
- 每项包含数值 `domain/code`、操作 `stage`、相关 `path`、源代码失败点 `source_file/source_line`；
  系统或 CRT 提供错误时记录 `native.source/code/category`，语法错误位置可用时记录从 1 开始的 `byte_offset`，
  字段校验失败时记录 `json_pointer`（组合约束可能定位到整个对象）。
- 采集预算固定为 8 项；每项 stage/path/source_file/native category 分别最多 48/512/256/32 字节。
  JSON Pointer 最多 256 字节。项数或文本超限设置 `truncated=true`，主错误不会被后续错误挤掉；文本由报告按值拥有。
- 文件提交检查 write、flush 和 close，关闭成功后才报告写入成功。失败或输入无效时分别尝试清理两个输出；
  清理失败设置 `cleanup_incomplete=true` 并保留相关路径和错误，不宣称已全部回滚。
- 输出覆盖输入的冲突在修改文件之前拒绝。两个输出先检查路径别名，并在首个文件创建后按真实文件身份再次核对，
  处理大小写折叠等文件系统规则；发现别名时回滚已创建输出，不会继续写入摘要覆盖 canonical。

报告在操作开始前准备固定预算的自有存储；Probe现场位于预先分配的独立存储，避免多个报告占用过大的栈空间，采集时不扩容。存储准备、最终 JSON 格式化及工具的文件读取/解析仍可能分配内存，不承诺可恢复 OOM。
这些类型仅为工具私有实现，不冻结 Foundation ErrorReport 或 Plugin ABI。

## 编译依赖与实验边界

- `AbiProfile.h` 使用 `<nlohmann/json_fwd.hpp>`，`Fingerprint` 实现在 `AbiProfile.cpp`；宿主工具和测试共享 `PanguAbiProfile`，不为 Runtime 新增 Module。
- 固定使用 v3.12.0；`json.hpp` 提供 `_json` / `_json_pointer`，测试仅在局部作用域显式引入 `nlohmann::literals`，上层 CMake 用 `JSON_USE_GLOBAL_UDLS=0` 禁止引入全局命名空间。
- `JsonExtras` CTest 验证版本、稳定版 DOM/Pointer/字面量及 `allow_exceptions=false` 的解析失败行为；不使用预览版零拷贝 API。
- 头文件来自同一正式版本的 submodule，不混用其他版本；详见 [ThirdParty 说明](../../Source/ThirdParty/NlohmannJson/README.md)。

## Linux C++ Collector

Linux 已实现私有后端 `LinuxCollector.{h,cpp}`，由统一 `PanguAbiProfileTool guard / compile / collect` 触发；不再在 Linux CMake 中拼写 ABI Profile 的人工字段。`Tests/AbiProfile/LinuxCollectorTests.cpp` 核验两个真实 ELF Runtime TU，`LinuxCollectorCliTests.cpp` 实测两个 TU 的 Clang 编译收据、llvm-ar STATIC 归档、Profile/Canonical/Fingerprint/complete.json 发布、Guard，以及删除收据后的拒绝/撤销。链接探针只进行宿主**链接及 ELF `NEEDED` 非执行检查**，不运行可执行程序。

**当前支持范围严格限定为已实测的 WSL Ubuntu 24.04 x86_64 native/glibc 2.39、LLVM Clang 23.1.0、LLVM libc++ 23.1.0（ABI namespace `__1` / ABI version 1）。** 从实际编译命令和目标 libc++ `__config_site`/Headers 读取 ABI 宏，核实 `/etc/os-release`、目标 glibc Header 与实际 CRT/Runtime 文件；`sdk_abi_id=linux-sysroot-ubuntu-24.04` 为该已核验原生 Sysroot 的受限身份。`--sysroot` 或不同 Linux 发行版、musl/ABI、非基线 CPU 配置必须拒绝，不能默认为兼容。不得从宿主 `uname` 猜测任意交叉 Sysroot。LLVM 目录内的 `ld.lld` 当前因缺少 `libicui18n.so.70` 无法启动；Collector 使用与当前 CMake Clang 驱动相同的默认链接链路，校验 GNU 链接器实际打开的 libc++/libc++abi/glibc Provider 和 ELF DT_NEEDED，而不是强制切换到不能运行的 LLD。

Linux Collector 的受限 ABI Profile 证据现已接入 Root 统一 C++ 构建事务和技术 STATIC 依赖闭包，旧 Windows CMake 采集脚本已移除。这里记录的是 Linux 后端的受限支持范围；最终 SHARED Loader 可信产物绑定仍未实现。

## 实际 CMake 依赖闭包与技术 STATIC SCC

`cmake/AbiProfile/AbiProfile.cmake` 已将构建元数据导出接入生产 Root Module：`pangu_export_abi_dependency_graph(OUTPUT ... ROOT ... TARGETS ... ALIASES ... MANAGED ...)` 要求明确枚举受控 CMake Target 和 Alias，在 CMake Generate 阶段使用 `TARGET_GENEX_EVAL` 求出当前配置的 `LINK_LIBRARIES` 与 `INTERFACE_LINK_LIBRARIES`，供私有 C++ `DependencyGraph` 解析器核验。列表保留直连顺序及必要重复项，并额外覆盖只出现在 INTERFACE 的传递依赖。Target 属性已丢失原始 `PUBLIC`/`PRIVATE` 调用来源，因此证据采用保守的 `direct`/`interface` 标签，**不宣称精确恢复最终链接命令顺序**。对于不能核实的 CMake Generator Expression、未知 C++/C/SHARED/OBJECT/IMPORTED 依赖直接拒绝。

CMake 导出的只有 Target 类型、IMPORTED/受管身份、当前配置求值后的 `SOURCES`、`INTERFACE_SOURCES` 与链接属性；不在 CMake 中推断 `kind`、`language` 或 `source_visible`。这些分类、传递源限制、依赖闭包和错误报告由 `Private/DependencyGraph.cpp` 统一执行。

`Tests/AbiProfile/Fixtures/CMakeLists.txt`、`AbiCollectionCMakeGraph` 用真实 CMake 4 Configure/Generate/Build 和 File API 证明 Release/Debug、Alias、重复直连、INTERFACE-only、技术 STATIC A↔B SCC、可达预编译库拒绝的行为。File API 的 `dependencies` 仅作为构建图佐证，它不会列出 INTERFACE、Alias、IMPORTED 等完整链接关系。

**当前接入范围**：统一 C++ `Collect()` 已强制解析配置对应的依赖图并验证闭包；不再把无效的 `{}` 图当成可用于发布的证据。若可达技术 STATIC 缺少独立核验的 Profile，则明确失败，不发布 `complete.json`。Windows、Linux、macOS、Android/iOS 的正式 Collector CLI 测试夹具现在均使用有效的受管 Target 依赖图，Root 已切换到统一 Guard/Launcher/Collect。

**独立 STATIC 证据通道（已完成）**：CMake 可从 Target 的私有 `PANGU_ABI_REQUEST_FILE` 属性导出当前配置求值后的 `request_evaluated`，交由 C++ 解析为绝对 `RequestFile`。`VerifyDependencyProfiles()` 对每个可达、非当前 Target 的技术 STATIC 读取 schema 2 请求，检查同一 SourceRoot/BuildRoot/配置/Host/Target 身份，要求独立 `GuardCollection()` 为 Current，再从完成输出读取 Profile 并进行逐字段比较；技术 STATIC 自身可作为采集根节点，不要求预存自己的完成记录。输入快照将已存在的依赖请求、库、Profile、Canonical、Evidence、Fingerprint 与 complete.json 归入 dependency digest，而非本 Target 的 compile digest。

**SCC 顺序与链接探针（已完成）**：`DependencyPlan::CollectionGroups` 现在按依赖优先顺序输出实际编译 Target，技术 STATIC 的 A↔B 被放入同一组，不为 INTERFACE 创建伪 Profile；无环依赖按 B→A→消费 Module 排列。`AbiCollectionCMakeGraph` 在 Windows、Linux、macOS 上另行构建一个最终 Host 链接探针：Root→TechA→TechB→TechA，其中后续 A 符号来自独立 TU，只有最终 Host 实现 `WitnessHostOnly()`。Linux 通过 Ninja 实际命令断言保留两轮技术归档项，macOS/Windows 则验证各自原生链接器成功解析。探针**不执行最终 EXE**；生产 Module 的真实技术 STATIC SCC 采集接线已完成，但这仍不构成最终 EXE/Loader 验收。

**SCC 组事务**：`Collect(request)` 现在自动处理根所在的技术 SCC。完整成员请求必须属于同一 SourceRoot/BuildRoot/配置/Host/Target，使用同一导出图和 Schema，且输出、库和对象清单不重叠。按成员 Target 名全序取得所有成员锁，撤销旧结果；锁覆盖观察、平台采集、逐字段相容检查及发布。组外依赖继续要求独立 Guard Current，组内成员不互等 Profile。所有成员的 v1 Schema、真实编译收据、对象/归档内容关联及来源输入核验通过后，先发布数据与成员完成记录，再在字典序首成员输出目录最后发布 `group-complete.json`。清单绑定成员名、完成路径与每个完成记录的 SHA256。失败撤销整组完成状态，清理失败仍明确报告。

**组缓存**：组内快照绑定其他成员的请求与库，不互相哈希生成的完成记录/Profile，避免摘要环。组封口清单绑定完成记录中的完整来源快照与收据。调用任一成员的 Guard 都刷新全组 source/header、工具、收据、库与结果内容，然后检查封口；任一成员变化使全组失效。缓存命中不启动 Clang 或链接探针。编译前失效及取消路径同样撤销整组状态。组外依赖快照会记录可见的组封口文件。

**生产构建接线（已完成）**：CMake 为每个成员生成现有 schema 2 请求、精确对象清单和编译数据库，在共享依赖图中导出完整成员的 `request_file` / `PANGU_ABI_REQUEST_FILE`。编译经过现有 `compile --request ... -- <compiler argv>` 生成真实收据；所有成员归档完成后，对该 SCC 任一成员调用一次 `collect --request`。组外依赖按 `CollectionGroups` 先完成，普通 Module 的 Guard 顺序由生产 CMake 接线保证。私有 C++ `BeginCollectionTransaction` / `CollectionTransaction::Publish` 覆盖采集全程持锁；`PublishCollectionGroup` 支持已准备好的组输入，仍核验来源与相容性。`AbiCollectionGroup` 用真实 Clang 和归档覆盖首次采集、缓存、部分发布/封口失败、跨成员输入/收据漂移、取消与实际不兼容编译参数。

**安全边界**：Host 自举和 Root 自动请求/Launcher/Guard/Collect 已接入生产 STATIC Module；Android/iOS 验证的是目标对象和归档及 Host 采集，不执行目标端 witness。最终消费者 EXE/SHARED Loader、跨进程加载及可信发布仍未验收；`GuardCollection` 只代表当前构建内有效性，不是签名或 Loader 信任。
