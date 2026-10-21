# Pangu ABI Profile / Fingerprint v1

> 设计状态：Frozen（Schema v1、Canonical Serialization、SHA-256 Fingerprint、严格兼容判定与装载校验顺序）；实际构建集成和公开二进制入口布局未完成
> 更新：2026-10-21
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
                 -> 版本化窄 C Discovery + 二次 Profile 校验
                 -> 受控 C++ 注册入口
                 -> Module Registration Transaction -> Active
```

1. **预检**必须在 OS 装载外部候选代码前尽可能通过非执行式读取完成版本/平台/ABI Profile 比较；侧载元数据与产物必须有可验证的关联，不能单凭可篡改 Sidecar 文件证明库本身一致。预检工艺/嵌入节、哈希/签名可信策略另行冻结。
2. `LoadLibrary/dlopen` **可能先运行全局构造或 DLL 初始化器**，故装载后二次校验仅保证调用 Pangu C++ 注册入口前已验证，不代表任意外部代码从未执行。v1 禁止依赖动态库全局构造完成业务注册；非可信库运行安全需独立解决。
3. SHARED 查询入口必须为版本化的窄 C 兼容发现协议，交换内容包含最小 Protocol Version、结构大小、Profile/Fingerprint、受控 C++ 注册函数入口信息。**精确符号名、POD 布局、字节序/对齐及入口签名不在本次冻结范围**。
4. 只有兼容性和 Module Interface Contract 检查通过，才可以进行 C++ 注册、使用 STL/Result 或发布 Service/Registry 能力；失败报告应保留可读字段差异，并按 Module v1 事务回滚。
5. 预检/二次校验不替代最后的 ModuleCodePin、Borrow、Allocator/GPU 安全卸载契约。

## 7. 验收矩阵与非冻结范围

Schema/测试向量的临时 **C++23 验证探针**位于本地 `.probe/abi-profile/`（被 `.gitignore` 排除，**不是正式 Tests，也不是版本化回归测试**），使用 Clang + CMake + CTest + RapidJSON。它仅覆盖本 Schema 使用的 JSON Schema 关键字，并核对 Canonical JSON/SHA-256 测试向量；不是完整 JSON Schema 实现，也不代表实际 Toolchain/Loader 已集成。正式 ABI 回归测试须在引擎 C++23 实现完成后接入仓库 CMake + CTest。仍必须验证：Canonical JSON/SHA-256 测试向量、字段顺序不变性、变异拒绝、同 ABI 跨 Target/C++ 标准库交换、Windows `_ITERATOR_DEBUG_LEVEL`/CRT 拒绝、libc++ ABI namespace/version 变异、Toolchain Patch 差异、每 TU 配置一致性、STATIC Catalog 组合、SHARED 预检+二次入口、ModuleCodePin/失败回滚，以及宿主与动态库生命周期。

平台验证目标依照现有 Toolchain 约束：Windows/WSL Linux/macOS 做本机跨库加载测试；Android/iOS 先完成目标编译链接与 ABI 采集，设备运行/打包必须独立验收。现有探针不自动代表正式引擎功能通过。

**尚未冻结**：AbiProfile/AbiFingerprint 的 C++ 对象内存布局、具体 Toolchain Probe 实现、精确 Runtime/SDK 身份提取工具、机器可读 Manifest/导出符号/POD、ModuleDescriptor Public API、公共接口签名与 API Surface Digest、包内可信元数据绑定、签名认证、跨版本兼容例外、多个 Host/Hot Reload。上述属于实现或后续专项协议，**不影响本次冻结的 JSON Schema、Fingerprint 算法与严格相容判定**。

## 8. 参考

- [RFC 8785 — JSON Canonicalization Scheme](https://www.rfc-editor.org/rfc/rfc8785)
- [JSON Schema — Numeric types](https://json-schema.org/understanding-json-schema/reference/numeric)
- [libc++ ABI Versioning](https://libcxx.llvm.org/DesignDocs/ABIVersioning.html)
- [libc++ Vendor ABI Options](https://libcxx.llvm.org/VendorDocumentation.html)
- [Microsoft STL / _ITERATOR_DEBUG_LEVEL](https://github.com/microsoft/STL/blob/main/stl/inc/yvals.h)
- [Microsoft detect_mismatch](https://learn.microsoft.com/en-us/cpp/preprocessor/detect-mismatch)
