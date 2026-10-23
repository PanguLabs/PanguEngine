# PanguAbiProfileTool — ABI Profile v1 采集与编码工具（Build-Time）

本工具由 CMake 的本机宿主构建生成，位于 `Build/<Platform>/Bin/PanguAbiProfileTool[.exe]`。依赖固定于 `Source/ThirdParty/NlohmannJson/Repo` submodule 的 nlohmann/json v3.12.0 正式发布版，构建前需初始化 submodule；仅包含工具代码和测试，不构成新的 Pangu Runtime Module。

原有四参数编码模式保持兼容。输入可使用下面正式 Windows 采集产生的 `profile.json`；手工 JSON 仅用于算法诊断，不能冒充真实构建配置：

```sh
Build/Windows/Bin/PanguAbiProfileTool.exe \
  wiki/foundation/abi-profile-v1.schema.json \
  <validated-input-profile.json> \
  Build/Windows/AbiProfile.canonical.json \
  Build/Windows/AbiProfile.sha256
```

当前支持：

- 先检查**冻结 Schema 文件本身**的固定 SHA-256（只规范化 CRLF/LF 换行，不接受调用方替换为更宽松的同名 Schema），再解析 JSON、拒绝重复 Key，并校验 v1 Profile 字段；
- 在 v1 JSON 约束下按 RFC 8785 JCS 递归排序对象 Key，保留数组顺序；
- 计算 SHA-256(`PANGU_ABI_PROFILE_V1\0` + UTF-8 canonical JSON)，输出小写十六进制 64 字符，不添加换行；
- 不符合 Schema / malformed / duplicate-key 输入时返回非零状态，**不生成结果文件**；
- 使用 `Tests/AbiProfile` 的正式 CTest 检查冻结向量及 Windows/Linux/macOS 平台组合约束。

**四参数编码模式的边界**：仅核验声明的 Profile JSON 内容格式和 Digest，不独立证明声明来源。正式 Windows 构建流程另行采集并核验编译/链接证据；两种模式均不能证明 Module 产物与预检材料的可信绑定、最终消费者/STATIC Catalog 汇合时的一致性、TOCTOU 或动态加载后的复核，不得据此放行生产 SHARED Loader。

此工具刻意不接入 Android/iOS 交叉编译：它运行在构建宿主，不应被当作 Android/iOS 目标程序执行；交叉编译情形下需由宿主阶段生成受控的 Profile。

## Windows 正式构建采集

当前首切片支持 **Windows native Ninja x64、非 UNITY STATIC Target**。根工程在创建宿主工具后调用
`pangu_enable_windows_abi_profiles(PanguFoundation PanguModule)`；该接线位于 `cmake/AbiProfile/`，
独立于 `Tests/` 与 `BUILD_TESTING`。其他平台继续保留编码工具及既有测试，正式采集会明确提示 pending。

```sh
cmake --preset windows -DBUILD_TESTING=OFF
cmake --build --preset windows
# 也可显式生成单个 Module 的库与 Profile：
cmake --build --preset windows --target PanguModuleAbiProfile
```

默认 ALL 构建为两个真实 Module 分别生成 `Build/Windows/Abi/<Target>/`：

| 文件 | 含义 |
| --- | --- |
| `request.json`、`objects.txt` | CMake 生成的私有采集请求和真实 Target 全对象清单 |
| `compiler-evidence.json` | 每个 TU 的实际参数、Clang 宏/前端输出、头文件来源和对象指令，以及观察到的运行库路径 |
| `profile.json`、`canonical.json`、`fingerprint.sha256` | 自动映射到冻结 v1 Schema 后，由现有编码器验证和生成的结果 |
| `evidence.json` | `controlled-windows-static-build` 范围声明，明确 `binary_binding=false`、`final_consumer_validated=false` |
| `work/` | 预处理 wrapper、未运行的链接探针及实际 LLD 选库日志等诊断材料 |

采集过程按对象输出路径精确关联 `compile_commands.json`，覆盖 Target 的全部 TU；同源多目标不会按源路径误取首项。
展开响应文件后重放实际参数，以原源文件的 include 加上布局探针读取有效宏、验证默认 packing/native enum，
逐 TU 检查 STL/SDK 实际头文件路径和配置。Clang resource Header 的转发 shim 与实际 MSVC STL Header 分别处理。
未知参数、未支持的布局/CPU 模式、缺失或重复对象记录、TU 配置不一致都会拒绝生成结果。
冻结 v1 没有独立的 MSVC STL ABI 配置字段，因此本切片只接受当前核验的 `_HAS_EXCEPTIONS=1`、
`_HAS_STATIC_RTTI=1`、`_CRT_PACKING=8`；源码内覆盖也会被实际预处理结果检出并拒绝。
其中 `_HAS_EXCEPTIONS` 是 MSVC STL 的 Header ABI 开关，与编译器 `-fno-exceptions` 分开检查；
不能只凭 `_CPPUNWIND` 关闭就认定不同 STL 配置的对象布局相同。

STATIC 只有归档步骤。为核验其 CRT/STL 链接政策，采集器将**全部真实对象**加入一个不运行的链接探针，
从 LLD 实际打开的库路径交叉核对 STL/SDK Header 所在 root、版本、架构及 SDK `Platform.xml` / MSVC 安装元数据。
这证明该次受控 witness 的选择，不证明未来 EXE/DLL 的最终链接；实际最终消费者仍需再次验收。
本切片显式拒绝除 `PanguBuildOptions` 之外的库依赖，直到实现真实依赖闭包传递；不静默漏掉依赖。

每个受管库的 Prepare 目标在编译前失效旧的五类输出；任何采集、编码或提交失败都会逐项清理，清理不完整会报错。
只构建原始库 Target 会失效旧结果，生成配套 Profile 应使用默认 ALL 或 `<Target>AbiProfile` Target。

新增内部映射入口为 `PanguAbiProfileTool --collect-windows <compiler-evidence.json> <profile-out>`。
它处理私有证据的数据校验与映射；证据真实性来自上述编译/链接编排，手写该输入本身不构成来源证明。
此实现不冻结新的 Public C++ API、采集请求协议或 Loader Manifest。

## 失败报告与文件状态

原四参数命令保持不变；新增映射模式复用相同失败报告。失败时以非零退出码表示结果分类，stdout 输出一行独立的 JSON 失败报告，
stderr 只作简短提示。调用方应分别捕获退出码和 stdout；即使 stderr 被重定向，报告仍可取得。

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

报告的采集存储固定，最终 JSON 格式化及工具的文件读取/解析仍可能分配内存，不承诺可恢复 OOM。
这些类型仅为工具私有实现，不冻结 Foundation ErrorReport 或 Plugin ABI。

## 编译依赖与实验边界

- `AbiProfile.h` 使用 `<nlohmann/json_fwd.hpp>`，`Fingerprint` 实现在 `AbiProfile.cpp`；宿主工具和测试共享 `PanguAbiProfile`，不为 Runtime 新增 Module。
- 固定使用 v3.12.0；`json.hpp` 提供 `_json` / `_json_pointer`，测试仅在局部作用域显式引入 `nlohmann::literals`，上层 CMake 用 `JSON_USE_GLOBAL_UDLS=0` 禁止引入全局命名空间。
- `JsonExtras` CTest 验证版本、稳定版 DOM/Pointer/字面量及 `allow_exceptions=false` 的解析失败行为；不使用预览版零拷贝 API。
- 头文件来自同一正式版本的 submodule，不混用其他版本；详见 [ThirdParty 说明](../../Source/ThirdParty/NlohmannJson/README.md)。
