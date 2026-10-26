# Pangu ABI Profile / Fingerprint v1

> 设计状态：Frozen（Schema v1、Canonical Serialization、SHA-256 Fingerprint、严格兼容判定与装载校验顺序）；五平台 STATIC 构建集成已完成，最终 EXE/SHARED Loader 与公开二进制入口布局待实现
> 更新：2026-10-27
> 决策：[ADR-0009：ABI Profile 与 Fingerprint v1](../decisions/0009-abi-profile-fingerprint.md)
> 基线：[ABI](abi.md)、[工具链](toolchain.md)、[构建系统](build-system.md)、[Module 注册与生命周期](../core/module-registration-lifetime.md)、[内存管理](../core/memory-management.md)

## 1. 目标与适用范围

v1 对同一受控构建、同一目标进程中的 C++ Module 定义**可复现、保守、失败时拒绝（Fail Closed）**的 ABI 配置兼容闸门。Windows、Linux、macOS、Android、iOS 各自使用实际目标 ABI；不同目标平台的二进制不得互换。ABI Fingerprint 相等是受控 C++ 互操作的**必要条件而非充分条件**。

- `AbiProfile`：真实构建参数、C++ ABI、标准库及运行库契约的结构化记录。
- `AbiFingerprint`：对 **Canonical AbiProfile** 的 32 字节哈希；仅用于快速比较与定位差异。
- `BuildIdentity`：构建时间、Git Commit、二进制内容身份、构建机器与 CI 信息；不属于该 Fingerprint。
- `Module Interface Contract`：API/类型版本与公共类型一致性；不由 Fingerprint 自动证明。
- `ModuleId`、`ServiceId`、Error Domain ID 是**不同身份域**，不得借用 Fingerprint 代替。

即使 Fingerprint 完全相同，仍必须满足 Public Header 的 ODR/Layout、导出符号、Version/Contract、Allocator Provenance、借用及 CodePin 等约束。Fingerprint 不是数字签名，也不是运行不可信 DLL 的安全沙箱。

## 2. v1 冻结的 Profile Schema

采用单个顶层 JSON Object，固定字段 `schema`、`policy`、`target`、`compiler`、`language`、`stdlib`、`runtime`；**所有字段均为必填**。`schema` 为 JSON 整数 `1`；其他叶子字段均为 ASCII 字符串，`language.abi_options` 为排序的 ASCII 字符串数组。没有可选字段、隐式默认值或任意拓展字段。结构由 [机器可读 JSON Schema](abi-profile-v1.schema.json) 同步约束，正文是规范性定义。Schema 的 `oneOf` 和条件规则直接校验 Windows、Linux（glibc/musl）、macOS、Android、iOS 的 Target Triple、Object Format、C++ ABI、STL、Runtime、Compiler 来源及其组合。**Schema 不能证明字段真的来自实际编译器和 SDK**，来源校验由第 6 节构建采集流程完成。

| 分组 | 必需字段 | 规则 |
| --- | --- | --- |
| 顶层 | `schema`, `policy` | 固定为 `1`、`strict-v1` |
| `target` | `triple`, `object`, `pointer_bits`, `endian`, `cpu_abi` | 有效 LLVM 目标 Triple（规范化小写）；对象格式 `coff/elf/macho`；位宽 `32/64`；端序 `little/big`；CPU ABI/调用平台约定身份 |
| `compiler` | `id`, `distribution`, `version`, `cxx_abi` | `id=clang`；实际受控 Clang 发行来源；真实完整版本（含补丁）；C++ ABI `msvc/itanium` |
| `language` | `standard`, `exceptions`, `rtti`, `abi_options` | 固定 `c++23`、`off`；RTTI `on/off`；八项 ABI 布局/调用政策见下 |
| `stdlib` | `id`, `version`, `abi_version`, `abi_namespace`, `iterator_debug_level` | Windows `msvc-stl`；其他平台 `libc++`；版本从目标**实际头文件与配置**提取，ABI namespace/mode 必须反映实际构建 |
| `runtime` | `crt`, `cxx_runtime`, `cxxabi_runtime`, `sdk_abi_id` | CRT、C++ Runtime 及 ABI 支撑库链接政策与目标 SDK/NDK/Sysroot ABI 身份 |

`target.triple` 须由实际编译前端与目标参数确定，而不是手工填写架构名。`compiler.distribution` 仅允许 `llvm-upstream`、`macports`、`android-ndk`，须由已核验的 Toolchain 来源导出；每个平台允许的来源见 Schema。`compiler.version` 为检测到的完整数字版本令牌 `major.minor.patch[.revision]`；Windows/Linux/macOS/iOS 使用 Clang 23.x，Android 使用选定 NDK 自带 Clang。**v1 不放宽 Clang Patch 差异**，若后续认证兼容，须升级兼容策略版本。

`stdlib.version` 记录实际所用 Header 实现版本（Windows 可由 MSVC STL 版本宏及更新宏组成；libc++ 来自实际版本/配置）。`stdlib.abi_version` 在 Windows 使用 `msvc-stl-<abi-family>`，在 libc++ 使用实际 `_LIBCPP_ABI_VERSION` 的正整数十进制字符串；不得混用或填入 `na`。`stdlib.abi_namespace` 在 Windows 为 `na`，在 libc++ 为真实 inline ABI namespace，不能将未知值伪装为 `na`。`stdlib.iterator_debug_level` 在 Windows 为检测的 `0/1/2`，其他平台必须为 `na`。libc++ 的 ABI 配置须在编译探针里捕获，不仅比较 `_LIBCPP_VERSION`；不能用相同 ABI 版本号掩盖单独改变布局的配置宏，v1 无法表达的差异必须拒绝或经新版 Schema 纳入。

`runtime.crt` 在 Windows 的规范值为 `md-release`、`md-debug`、`mt-release`、`mt-debug`，必须与 `msvc-stl-dynamic/static` 匹配；Linux 为 `glibc/musl`，Android 为 `bionic`，Apple 为 `apple-libsystem`。`sdk_abi_id` 表达经核验的目标 SDK/NDK/Sysroot、链接运行库或 Toolset 的**版本化受控身份**：使用 `windows-sdk-<sdk-version>-msvc-<toolset-version>`、`linux-sysroot-<distribution>-<version>`、`android-ndk-r<release>-api-<level>`、`macos-sdk-<version>` 或 `ios-sdk-<version>` 格式（精确正则见 Schema）。实际内容由 Toolchain Probe 采集，不能以匹配正则代替来源核验。v1 对身份变化保守拒绝；不能由此声称 SDK Patch 差异一定破坏 ABI。

### 八项 `language.abi_options`（必须完整提供）

数组精确包含以下八个键，各出现一次，格式为 `key=value`，**按键 ASCII 升序排列**：

1. `bitfield_layout`：位域布局规则（例如 `msvc`/`itanium`）
2. `calling_convention`：默认接口调用约定（例如 `msvc`/`sysv`/`aapcs`）
3. `char_signedness`：`signed`/`unsigned`
4. `enum_width`：`native`/`short`
5. `float_abi`：目标浮点调用 ABI（例如 `default`/`hard`/`soft`）
6. `struct_pack`：默认 packing（例如 `default`/`pack1`/`pack2`/`pack4`/`pack8`/`pack16`）
7. `vector_abi`：目标 Vector 调用/传参 ABI 政策的受控身份
8. `wchar_width`：实际编译目标的 `16`/`32`（不是臆测的“native”）

这些值从有效编译选项、目标 ABI 和编译探针推导；一旦出现无法分类的 ABI 关键编译选项或关键 Header 宏差异，构建必须拒绝产生“已验证”Fingerprint。编译单元的 `#pragma pack` 滥用、条件编译、Public Header ODR 变化不能只靠此八项发现，仍需要 Public ABI Contract 和测试。目标级一致性并不自动等于每个 Translation Unit 都一致。

`calling_convention` 的平台与架构条件必须共同满足：Windows 的 x86/x64 使用 `msvc`；**Windows ARM 不属于项目目标，Schema 必须拒绝其 Profile**。其他平台的 x86/x64、ARM32、ARM64 分别使用 `sysv`、`aapcs`、`aapcs64`。这些是本协议的约定身份，不表示各平台 ARM ABI 完全相同，差异仍由 Target Triple 等字段区分。Vector ABI 除已确认的 `default` 外，只允许 x86/x64 对应的 `msvc-vectorcall` 或 `sysv-vector`，以及 ARM64 的 `aapcs64`；未列出的映射须拒绝。

### v1 有限枚举与确定性采集规则

| 字段 | v1 采集与取值约束 |
| --- | --- |
| `target.triple`、`object`、`cpu_abi` | 使用实际编译 Target、CPU ABI 与影响调用约定的选项确定。`cpu_abi` 仅允许 Schema 的 `baseline-x86_64`、`x86_64-v2/v3/v4`、`baseline-x86`、`aapcs64/aapcs32`，与 Triple 架构、指针宽度匹配；不允许 `unknown` |
| `compiler.distribution`、`version` | 发行来源仅允许 `llvm-upstream`、`macports`、`android-ndk`；版本必须来源于实际 Clang，规范化为数字 `major.minor.patch[.revision]`，不得随意手填 |
| `language.abi_options` | 每个取值使用 Schema 内的受控枚举。`vector_abi=default` 仅表示已确认的目标默认规则且无 ABI 改写参数，不是无法判断时的占位符 |
| `stdlib.version`、`abi_version`、`abi_namespace` | 由目标 C++ STL 头文件、ABI 配置宏和实际 Runtime 确认；`na` 仅在该属性确实不适用时使用 |
| `runtime.*` | 来自链接命令、CRT/ABI Runtime 选择与 SDK/NDK/Sysroot 版本信息；`sdk_abi_id` 按本节受控格式构造，不得接受未核验的标签 |

**JSON Schema 负责结构、枚举、目标平台组合约束，不负责来源真实性。** 生成器必须基于真实编译命令、目标标准库头文件宏及 SDK/NDK/Runtime 信息逐个验证 ABI 关键 Translation Unit；即使某个伪造值满足正则，也不能当成已验证 Profile。碰到尚未定义映射的目标 CPU/ABI/工具链必须拒绝，新增合法组合需更新规范与测试。

## 3. Canonical Serialization v1（Frozen）

1. `schema=1` 且 `policy=strict-v1`；只接受第 2 节规定的 Object、字段和类型。Unknown/Missing/重复字段、错误类型、字段值为空、错误平台组合和不满足数组约束 **MUST 拒绝**，不能忽略未知属性来维持指纹相同。
2. 所有名称和字段值使用固定的 ASCII 令牌（版本与目标具体编码按受控 Toolchain 采集约定）；普通令牌只含 `[a-z0-9_.:+/-]`，`abi_options` 额外允许键值之间唯一的 `=`。不允许任何空白（包括末尾换行）、Locale 转换、大小写随意混用、文件绝对路径、生成时间或环境地址。字符串值统一小写，版本字面量不能丢弃补丁号。仅 `schema` 是数值，输入词法固定为 `1`，不接受 `1.0`、`1e0`、NaN 或 null；JSON Schema 按数学值把 `1.0` 也视为整数，故此词法要求与重复键拒绝均须在 JSON 解析阶段额外检查。
3. 序列化严格使用 **RFC 8785 JSON Canonicalization Scheme（JCS）**，输出 UTF-8 无 BOM、无额外空白/换行、对象键递归排序，数组顺序保持不变。由于本 Schema 键和值均限 ASCII 且不存在浮点值，可直接用严格的 RFC 8785 实现，不另造一套“差不多的 JSON”。
4. `abi_options` 必须在**输入验证阶段**按规范验证排序/唯一性；不得用重新排序掩盖原始输入违反协议。JSON 对象的键排序则属于 RFC 8785 的正常规范化操作。
5. 若未来增加字段、改变枚举含义、允许非严格比较或更新编码规则，必须更新 Schema/Policy 版本，不能在原 v1 字节协议上静默扩展。

## 4. Fingerprint v1（Frozen）

```text
domain_prefix = ASCII("PANGU_ABI_PROFILE_V1") || 0x00
canonical     = RFC8785(ValidatedAbiProfileV1)
digest        = SHA-256(domain_prefix || UTF8(canonical))
```

- Digest 固定 **256 位（32 字节）**，二进制原始存储使用 SHA-256 输出的正常字节顺序，无截断、无平台字节序变换；可读形式为 **64 个小写十六进制字符**。
- 域前缀精确为 21 字节（ASCII 字符串加一个 NUL 字节），不得加入换行、长度字段、额外 NUL 或 JSON 文本双引号。机器向量 JSON 用 `\u0000` 代表真实的 U+0000/0x00 字节，不得把字面量 `\\x00` 当作 NUL。
- Hash/Canonical 在 **Build-Time** 生成，Runtime 使用预生成结果做加载期比较；不在每帧/每次跨模块调用时重新序列化或哈希。
- 接收方在装载校验时必须按本协议验证 Profile，并复算 Digest 与产物声明值比对；只有一致的 Profile/Digest 对才可进入兼容比较并缓存结果。预检材料与装载后嵌入数据也须一致，不能仅凭声称的 Fingerprint 相等放行内容不同的 Profile。
- Fingerprint **不等于签名**、不验证产物来源，不包含 BuildIdentity/Git Commit/构建路径/时间，也不包含 ModuleId、PluginId、服务或错误身份。ABI 与具体 API 版本分别检验。
- 严格等值是 v1 唯一兼容性判定策略；不基于 Clang 主版本、同一操作系统或标准库品牌做宽松推断。

### 规范测试向量

[机器可读测试向量](abi-profile-v1.vectors.json) 包含一个**纯虚构的 Windows 配置示例**和一处 `iterator_debug_level` 变异。示例字段仅用于测试编码及算法，不代表实际 PanguEngine 已安装的 MSVC STL 或 SDK 版本。

其中 `schema_cases` 是结构校验正反例：以 `baseline.profile` 为基底，将 `replacements` 中的 JSON Pointer 对应字段替换后，校验结果必须等于 `valid`。这些例子验证未知架构、末尾换行、平台 ABI 混用，以及非目标 Windows ARM32/ARM64 的拒绝，不替代真实编译来源验证。

- 原始 Profile 的 Canonical JSON 为 **779 字节**。
- 原始 SHA-256：`65e540973f0449be272a73e34753cfa71e4f91e12fdd901a6f5d65f8c5a5389c`。
- 仅将 `/stdlib/iterator_debug_level` 由 `"0"` 改为 `"2"`：新 SHA-256 必须为 `c39f19b0e2a8e3582779e27d4cb53282058c023bc5d398f8ff4e231aa3f92863`。
- 对 Profile 中任意 Object 属性进行重新排序，Canonical 和 Digest 必须不变；改变路径、构建时间和 BuildIdentity（均不属于 Profile）也不应改变指纹。
- 有字段缺失、额外字段、重复键、不合法 `abi_options` 顺序、非法值或未知 Schema/Policy 时，必须拒绝而非生成 v1 Fingerprint。
- 合法 Profile 配上过期或错误的 Digest 必须在装载校验中拒绝；JSON Schema 只校验 Profile，不能单独完成这项一致性检查。

## 5. 兼容性与升级政策（Frozen）

| 情况 | 处理 |
| --- | --- |
| 两端 Profile Schema/Policy 合法且 Fingerprint 完全相同 | 通过 **ABI 配置闸门**，再进行 Module Protocol、Public Interface、Identity、Ownership 与 Lifecycle 检查 |
| 必需字段不同、Schema 未知、Version/RTTI/CRT/STL ABI 不同 | **拒绝 C++ 边界**（Fail Closed） |
| Android NDK Clang 与另一目标不同主版本 | **不**要求跨 OS 比较；按对应目标进程自身 Profile 判断 |
| Compiler Patch 或 SDK/NDK ABI 身份变化 | v1 保守拒绝；验证兼容后用**新版 Policy**允许，不能随意绕过 |
| Fingerprint 相同但导出 API/公开类型不兼容 | **拒绝**；Profile 不是 Public API Surface Digest |
| Fingerprint 相同但 Allocator/CodePin 约束未满足 | **拒绝相应操作或卸载**；ABI 兼容不等于对象所有权安全 |
| 只有独立、版本化稳定 C ABI 的边界 | 单独执行 C ABI 握手，不得直接交换不兼容 C++ 对象 |

这里的拒绝既适用于 SHARED，也适用于静态组合中的兼容性校验。不同 Module 的 `BuildIdentity` 可以不同，但在 v1 受控 C++ 互操作场景，AbiProfile 必须严格一致。

## 6. 构建和装载协议（语义/顺序 Frozen）

### 构建与 STATIC

- CMake 4.0+ 是 Module Target/编译链接配置权威来源；每个 Module 一个真实 STATIC/SHARED Target，不增加 ABI 专用 Module Library 或 BinaryImage 层。
- 配置与生成阶段提取实际 Clang、Target Triple、标准库、Runtime 和 ABI 选项；任何未能确定的重要字段/ABI 宏差异都使“可安全跨 Module C++ 交换”的构建失败，不允许用户手填 Fingerprint 冒充采集结果。
- 严格校验每个 C++ TU 的实际 ABI 有关编译命令和公共接口编译政策；不能仅采样一个 Compile Command。预生成值关联实际 Module 产物与静态 Catalog；使用宿主侧受控工具输出 Canonical JSON 与 Fingerprint。
- STATIC Module 进入 EXE/DLL/SO 时，在最终链接/装配阶段检查该受管进程内适用 C++ 互操作的 Profile；成功链接并不自动意味着 ABI/ODR 一致。实际被链接入的 STATIC 代码，CodePin 覆盖其真正的承载库。

### SHARED

```text
CMake Target -> Compiler / Runtime Probe -> Canonical Profile + SHA-256
                 -> 受控产物/预检材料关联
                 -> 无执行的装载前兼容性 Preflight
                 -> OS LoadLibrary / dlopen
                 -> 稳定导出名定位原生 C++23 Query + 二次 Profile/接口契约复核
                 -> 统一 C++23 Registration
                 -> Module Registration Transaction -> Active
```

1. **预检**必须在 OS Load 前以非执行式读取完成版本/平台/ABI Profile、Fingerprint 和原生 C++ ModuleDefinition Public API Contract 的严格验证；预检数据必须与**实际将加载的产物**可验证地绑定，不能单凭可篡改 Sidecar 认定一致。若产物身份、预检材料、ABI/API 合法性或预检后到 OS Load 的 TOCTOU 风险无法确认，应 Fail Closed、禁止 OS Load/C++ Query。预检工艺/嵌入节、可信绑定和签名策略另行冻结。
2. `LoadLibrary/dlopen` **可能先运行全局构造或 DLL 初始化器**。统一 C++23 Query 路线因此要求在 OS Load **之前**完成真实产物绑定和受控 C++ ABI/Public API 预检；装载后二次校验仅复核一致性，不能宣称阻止任意加载时执行。v1 禁止依赖动态库全局构造完成业务注册；非可信库运行安全需独立解决。
3. SHARED 发现入口由 [ADR-0010](../decisions/0010-unified-cpp-module-entry.md) 修订为**版本化稳定 `extern "C"` 导出名，仅用于定位原生 C++23 `ModuleDefinition` Query**，STATIC/SHARED 共用注册接口，**不需要独立窄 C POD Discovery**。由于 Query 即交换受控 C++ 类型，在执行 OS Load / C++ Query **之前**必须通过与实际加载产物可验证绑定的元数据，完成 ABI Profile/Fingerprint、协议版本和 C++ ModuleDefinition Public API Contract 的预检；预检无法验证即 Fail Closed。加载后 Query 的 Descriptor/Canonical/Digest 与预检结果必须复核一致，成功才进入注册。**精确符号名、C++ Public API 布局与调用约定、可信预检产物绑定/TOCTOU 工艺仍未冻结**。
4. 只有兼容性和 Module Interface Contract 检查通过，才可以进行 C++ 注册、使用 STL/Result 或发布 Service/Registry 能力；失败报告应保留可读字段差异，并按 Module v1 事务回滚。
5. 预检/二次校验不替代最后的 ModuleCodePin、Borrow、Allocator/GPU 安全卸载契约。

## ABI Profile v1 的最新构建期实施状态（2026-10-27）

冻结 v1 **JSON Schema、RFC 8785 Canonical 和 SHA-256 Fingerprint 算法保持不变**。在这一协议之上，五平台 STATIC ABI Collection 的 Task 1—12 已全部完成，覆盖 Host Guard → 真实 C++ Compiler Launcher/编译收据 → 原生 Library Target 归档 → C++ Collect/完成记录流程。原 R1—10 及提交 `9561fe6` 的追加评审问题均已修复并独立复审通过，无剩余 Critical/Important。

- 生产 Root 使用 `pangu_register_abi_module(Target)`、`pangu_register_abi_static(Target)` 和 `pangu_finalize_abi_profiles(TARGETS ...)`；一个受管 Module 只有一个原生 STATIC Library Target，不另创 Profile Library。旧 `Windows.cmake`、`CollectWindows.cmake`、`WindowsRuntime.cmake`、`Prepare.cmake` 及 `--collect-windows` 已从生产入口移除。
- CMake 负责真实当前配置的 Target/Generator Expression/INTERFACE 传递依赖排序；C++ 负责依赖闭包、技术 STATIC SCC 批次采集、组 `group-complete.json` 封口、ABI 字段逐项相容和严格对象来源验证。未知关键 ABI 输入或无匹配收据对象 Fail Closed。
- 完成的受管 Target 产出 `<BuildRoot>/Abi/<Config>/<Target>/{profile.json,canonical.json,fingerprint.sha256,evidence.json,complete.json}` 等私有构建状态。最近一次完整 CTest 为 Windows **36/36**、Linux **34/34**、macOS **34/34**，合计 **104/104**；Android/iOS ARM64 由 Host 完成实际目标对象、STATIC 归档、Profile 采集、增量 Guard 和完成引用摘要校验，不执行目标程序。
- Windows Release MD/Debug MD/Release MT 的受管 Module 构建、采集及增量验证通过；Foundation/Module 同配置指纹一致，跨 CRT 配置指纹不同。七种配置的构建前后源码摘要一致。CMake 验证使用 Windows/macOS 4.4.3、Linux 4.4.2；保持最低 CMake 4.0 的声明并核对所用属性的最低版本，未单独运行 4.0.x 二进制，该项不作为本轮未关闭门禁。
- **绝不扩大保证范围**：`complete.json` 表示当前构建内的受控 STATIC 证据，不表示实际最终消费者 EXE/SHARED、OS Load 前后身份、签名/可信发布、TOCTOU、跨编译器不受控 C++ 产物或 Loader API/ODR 已验收。下方按日期记录的 2026-10-22～24 部分为**历史阶段快照**，不代表最新 Root 接线状态。

完整 CLI 与文件布局见 [ABI 工具说明](../../Tools/AbiProfile/README.md)，当前验收状态与历史记录见 [Implementation Plan](../../docs/superpowers/plans/2026-10-23-abi-collection.md)。实现主体原提交为 `9561fe6`；追加评审修复与文档已按用户授权通过 amend 合入同一提交，未推送。以下带日期的阶段记录保留当时结论，不作为当前待办。

## Build-Time 编码工具的历史阶段实现进展（2026-10-22）

已新增正式 C++23 `Tools/AbiProfile`（构建宿主上的 `PanguAbiProfileTool` 可执行程序，**非** Pangu Runtime Module）与 `Tests/AbiProfile`（`AbiProfileVectors`、`AbiProfileCli`、`JsonExtras`、`AbiProfileIo` 四组 CTest）。之前被忽略的 `.probe/abi-profile` 只作为设计验证历史保留，正式回归已经进入版本化 `Tests/`。工具通过受控的 header-only nlohmann/json v3.12.0 正式发布版（`Source/ThirdParty/NlohmannJson/Repo` Git submodule，许可证保留在上游仓库，外层使用 INTERFACE Target；ABI 工具及测试共用仅宿主侧的 `PanguAbiProfile` STATIC 库，不新增 Runtime Module）读取 JSON。

目前工具可以对提供的 Profile 进行 v1 Schema 子集词汇的 **显式支持校验**、严格值/平台组合检查、重复 JSON Key 拒绝、冻结 Schema 的内容 SHA-256 校验（只归一化 CRLF/LF，防止替换成宽松规则）、v1 约束下的 RFC 8785 Canonical JSON、领域分隔 SHA-256，并输出两个构建期文件；失败返回非零且清理此前可能残留的输出，拒绝输出路径覆盖 Schema/Profile 输入文件。Frozen Windows 向量 Canonical 长度为 779 字节，Baseline 与 Iterator Debug Level 变异的两组 SHA-256 已纳入正式回归；测试覆盖五平台的合法/非法 Profile 组合与格式拒绝，桌面 Windows、WSL Linux、macOS 均运行通过。

原四参数编码模式只保证 Profile 声明内容与 Fingerprint 算法，不从人工 JSON 推断受信任身份；其 stderr 仍标明 `provenance and TU ABI options NOT ATTESTED`。2026-10-23 增加了下面的 Windows 正式采集切片。其他平台的自动采集、最终消费者校验与可信产物绑定仍未完成；**不得**把该 CLI 据摘要相等接入生产 SHARED Loader 放行候选库。参见 [工具使用说明](../../Tools/AbiProfile/README.md)。

2026-10-23 补充了原始 NUL、输出路径别名、独立输出清理、真实 Close 失败及有界报告回归。
文件提交显式检查 write/flush/close；清理逐项尝试，无法完成时在独立 stdout JSON 报告中标明
`cleanup_incomplete` 并保留主错误和清理错误，不宣称回滚成功。报告和预算详见工具说明，
不由此冻结通用 Foundation ErrorReport 或扩展已冻结的 ABI Profile 字节协议。

## 7. 验收矩阵与非冻结范围

### Windows 正式采集首切片（2026-10-23）

`Tools/AbiProfile` 与 `cmake/AbiProfile/` 已为 Windows native Ninja x64 的 `PanguFoundation`、`PanguModule`
接入独立于测试总开关的自动采集。实际对象清单与编译数据库按 output 精确关联，检查每个 TU 的参数、
有效宏、前端 Target、实际 STL/SDK 头与 COFF 运行库指令；用全部真实对象的未运行链接探针核验 CRT/STL 选库、
SDK 版本化 Manifest 和 MSVC 工具集安装元数据，然后复用冻结 Schema/Canonical/Fingerprint 编码。
结果在 `Build/Windows/Abi/<Target>/`；Profile 内不写路径，来源材料另存私有证据。

当前支持非 UNITY STATIC、原生 x64、默认布局与受控参数集合；未知选项或无法表达的关键差异拒绝。
未支持真实库依赖闭包的 Target 会显式拒绝。开始编译前失效旧结果，上游编译失败或采集失败不能沿用旧指纹。
正式回归包括双 TU/同源多目标、第二个 TU 的冲突、响应文件及重复/缺失对象拒绝；私有证据字段不构成新冻结协议。

`evidence.json` 明确限定为 `controlled-windows-static-build`，并保留 `binary_binding=false`、
`final_consumer_validated=false`。这一实现补齐本切片的自动配置采集与 Profile 生成，**没有完成**最终消费者/STATIC Catalog
装配检查、Public Header ABI/ODR 验收、可信产物绑定、SHARED 预检或加载后复核。其他四平台的正式自动采集仍待实现。

Schema/测试向量的临时 **C++23 验证探针**位于本地 `.probe/abi-profile/`（被 `.gitignore` 排除，**不是正式 Tests，也不是版本化回归测试**），使用 Clang + CMake + CTest + 旧版 JSON 探针实现。它仅覆盖本 Schema 使用的 JSON Schema 关键字，并核对 Canonical JSON/SHA-256 测试向量；不是完整 JSON Schema 实现，也不代表实际 Toolchain/Loader 已集成。正式 ABI 回归测试须在引擎 C++23 实现完成后接入仓库 CMake + CTest。仍必须验证：Canonical JSON/SHA-256 测试向量、字段顺序不变性、变异拒绝、同 ABI 跨 Target/C++ 标准库交换、Windows `_ITERATOR_DEBUG_LEVEL`/CRT 拒绝、libc++ ABI namespace/version 变异、Toolchain Patch 差异、每 TU 配置一致性、STATIC Catalog 组合、SHARED 预检+二次入口、ModuleCodePin/失败回滚，以及宿主与动态库生命周期。

平台验证目标依照现有 Toolchain 约束：Windows/WSL Linux/macOS 做本机跨库加载测试；Android/iOS 先完成目标编译链接与 ABI 采集，设备运行/打包必须独立验收。现有探针不自动代表正式引擎功能通过。

**尚未冻结**：AbiProfile/AbiFingerprint 的 C++ 对象内存布局、具体 Toolchain Probe 实现、精确 Runtime/SDK 身份提取工具、机器可读 Manifest、原生 C++ Query 导出符号/API 契约、ModuleDescriptor Public API、公共接口签名与 API Surface Digest、包内可信元数据绑定、签名认证、跨版本兼容例外、多个 Host/Hot Reload。上述属于实现或后续专项协议，**不影响本次冻结的 JSON Schema、Fingerprint 算法与严格相容判定**。

## 8. 参考

- [RFC 8785 — JSON Canonicalization Scheme](https://www.rfc-editor.org/rfc/rfc8785)
- [JSON Schema — Numeric types](https://json-schema.org/understanding-json-schema/reference/numeric)
- [libc++ ABI Versioning](https://libcxx.llvm.org/DesignDocs/ABIVersioning.html)
- [libc++ Vendor ABI Options](https://libcxx.llvm.org/VendorDocumentation.html)
- [Microsoft STL / _ITERATOR_DEBUG_LEVEL](https://github.com/microsoft/STL/blob/main/stl/inc/yvals.h)
- [Microsoft detect_mismatch](https://learn.microsoft.com/en-us/cpp/preprocessor/detect-mismatch)

## 2026-10-23：Compiler Evidence 的阶段性实现

`Tests/Toolchain/` 与 `Tests/Toolchain/AuditCompilerMacros.cmake` 已落地首批**从真实 CMake 编译命令得到的 Clang 宏证据**：两个正式 Pangu Module 源文件 + 一项普通 ThirdParty 源文件 + 一项 Pangu Module 策略探针。证据包含编译命令 SHA-256、宏文本 SHA-256、STL/CRT/RTTI/异常/CPU 等事实，输出 `Build/<Platform>/Abi/compile-evidence.json`，且明确标记 `trusted: false`。五平台均完成编译阶段审计；三个桌面平台新增成功与 RTTI/Exception 两组冲突拒绝测试。

**冻结的 JSON Schema、Canonical Serialization、Fingerprint 算法和兼容协议均未修改。** 当前宏证据尚不能证明 SDK/NDK/CRT 等实际链接来源，不能完整构造有效 v1 Profile、不能代表所有 TU，更不能证明 Profile 与实际产物的可信绑定。后续须结合原始 Toolchain、SDK 文件和真实链接命令 Fail Closed 地补齐字段，然后才能让正式 `PanguAbiProfileTool` 从已核验材料自动生成 Fingerprint。详见 [Toolchain 编译审计](../../Tests/Toolchain/README.md)。

## 2026-10-24：Windows C++ Collector（Task 5）

已实现 `Tools/AbiProfile/Private/WindowsCollector.{h,cpp}` 与独立 `Tools/AbiProfile/Probe/RuntimeWitness.cpp`：真正的 Windows Clang 预处理与头文件来源、MSVC Toolset 元数据、SDK `Platform.xml` 根元素版本、真实 COFF DefaultLib 指令及 LLD Runtime 库路径均进行核验，并调用冻结的 `BuildWindowsProfile`、Schema/Canonical/Fingerprint 映射。预编译检查不要求 `.obj` 存在；最终收集要求每个实际对象及其编译收据。运行库探针绝不把所有 STATIC Module 强拼进 EXE，合法 Host 未解析符号可延迟到最终消费者。Schema v1、SHA-256 算法不变。

`AbiCollectorWindows` 验证双 TU、同源其他 Target 隔离、缺失/重复对象、Windows response file 展开、无关编译开关与 ABI 关键宏冲突、无效 SDK/STL/CRT 来源等。`AbiCollectorWindowsCli` 用真实 `guard -> compile -> archive -> collect -> guard` 链路并验证 `complete.json`/receipt，额外由最终 Consumer 提供 Host 符号并仅链接、不执行。新旧 Collector Profile 与 Fingerprint 在 Release MD、Debug MD、Release MT 三种独立配置下保持完全一致。Windows 全量 CTest 30/30、macOS 全量 20/20；Linux 完整结果另见实施记录。

**当前边界**：Task 5 是 C++ 后端与独立 CLI 的验收，正式 Root CMake 仍暂用 `cmake/AbiProfile/CollectWindows.cmake` 采集。按照计划 Task 11，在依赖闭包、Host Tool Bootstrap 和 Module Target 强保证完毕后才切换正式构建并删除旧脚本。仅此阶段的 Profile 不能声明最终消费者 ABI/ODR 验证、签名、真实产物可信绑定或 SHARED Loader 放行。
## 2026-10-24：Linux C++ Collector（Task 6）

统一 C++ 工具的 Linux 后端已接通 WSL Ubuntu 24.04（`x86_64-unknown-linux-gnu`、glibc 2.39）与 LLVM 23.1.0/libc++ 23.1.0 的实际原生采集。`LinuxCollector.cpp` 对每个真实 TU 执行 Clang 宏/布局/Triple 与 libc++ `__config_site` 检查，确认 ABI namespace `__1`、ABI version 1、STL hardening/layout 宏、RTTI、Exception、指针/字符宽度与 CPU 基线一致。对象存在后通过 `llvm-readobj` 核对 ELF 格式，最小独立 witness 使用真实宿主链接器并由 `llvm-readobj --needed-libs` 非执行式核验 libc++/libc++abi/glibc 的动态依赖与实际链接库路径；不会强拼 STATIC Module、不会执行 witness。SDK 身份来自已核验的目标原生 Sysroot（`/etc/os-release` + glibc Header/Runtime），**拒绝外部未知 Sysroot、musl 与未明确支持的分发版**，未将 Host `uname` 当目标身份。

Linux 专项 CTest 包含真实双 TU 正常采集、libc++ ABI/版本/hardening 与 glibc 身份变异、SDK/Runtime Provider 篡改拒绝、编译选项拒绝及生成文件硬链接保护。统一 CLI 集成回归执行实际 `guard → compile → llvm-ar → collect → guard`，验证两个编译收据及 `complete.json` 和指纹一致性，并模拟缺失收据后的撤销。本阶段**只表示 Build 内受控采集**；Loader 的二进制可信绑定、最终消费者与 API/ODR 校验，以及 Module 依赖闭包和 Root Target 强保证尚属于 Tasks 9–11。更广的发行版/CPU/交叉 Sysroot 未测试，不声称覆盖。
## 2026-10-24：Apple C++ Collector（Task 7）

已新增 `AppleCollector.{h,cpp}` 和 `Probe/AppleLibcxx.cpp`，仅在 macOS Host 采集 macOS x86_64 或 iOS ARM64：真实 TU 编译命令 `-isysroot` 与 `-###` Triple、MacPorts libc++ Headers/`__config_site`、SDKSettings.json/Plist、Apple SDK 的 libc++/libc++abi/libSystem `.tbd` 和实编译 Mach-O 对象互相核对。独立 witness 实际链接 Apple 运行库，并用 `llvm-readobj --needed-libs` 和 `otool -l` 非执行式校验架构、SDK 和 Deployment Target；禁止在 Host 上执行 iOS 二进制。MacPorts C++ Header 23.1.2 与 Apple 系统 dylib Current Version 1800.105 是不同来源，不以版本号相等作兼容断言。

`AbiCollectorApple`/`AbiCollectorAppleIOS` 测试非法 SDK、Target/ABI 参数、修改的 Runtime/Mach-O 证据及 APFS 符号链接和硬链接路径攻击。`AbiCollectorAppleCli`/`AbiCollectorAppleIOSCli` 验证两份真实收据、STATIC 归档、Profile/Canonical/Fingerprint/complete.json 完成与失效撤销；`AbiCollectorAppleProfilesDiffer` 要求 macOS SDK15.2 与 iPhoneOS SDK18.2 的 Profile/指纹确实不同。macOS 25/25 CTest 与 clang-tidy/clang-format 通过；iOS ARM64 STATIC 构建与 Mach-O 检查通过，**未执行 iOS 目标程序**。

这仅证明受限工具链的 Build/Compiler Profile，尚未满足 Task 9/11 的完整依赖闭包、最终产物可信绑定与 Loader 放行。

## 2026-10-24：Android C++ Collector（Task 8）

Windows Host 已接入 `AndroidCollector.{h,cpp}` 和 `Probe/AndroidLibcxx.cpp`，仅支持当前核验过的 NDK r30 / Clang 21.0.0 / ARM64 / API 29。以真实 `compile_commands.json` 和 `source.properties` (`Pkg.ReleaseName=r30`, Revision `30.0.16248370`) 为准，校验 NDK libc++ 21 的 ABI Namespace `__ndk1`、Bionic SDK/Runtime 文件、Target Triple、实际编译选项及 `llvm-readobj` ELF `EM_AARCH64`；原生 Android 默认 `-fno-signed-char`，Profile 必须保留 `char_signedness=unsigned`。Header 通过真实 Clang `-H` 采集进入输入快照，完整 NDK Revision 与绝对路径只留在证据中，冻结 Schema/Canonical/Fingerprint v1 不变。

独立 witness 由 Windows Host 的 NDK Clang 21 实际链接、静态读取 ELF `libc++_shared.so`/Bionic `libc.so` 动态依赖，**不会在 Windows 执行 Android 程序**。`AbiCollectorAndroid` 验证缺失/篡改 NDK、SDK、Runtime/ELF、API、ABI Macro、未知布局开关和硬链接路径；`AbiCollectorAndroidCli` 用真实 Windows quoting 走通 `guard→compile×2→llvm-ar→collect→guard`，校验两份编译收据、Profile/Canonical/Fingerprint/complete.json，再删除一份收据验证撤销。Windows CTest 32/32、Linux 22/22、macOS 25/25 通过，Android ARM64 STATIC 构建和 ELF 目标验证通过。当前仅为受控 Build Profile 的阶段验收；完整依赖闭包、Host Tool Bootstrap、Root Module 构建强保证及 Loader 二进制可信绑定仍在 Tasks 9–12。
