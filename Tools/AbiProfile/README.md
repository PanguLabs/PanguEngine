# PanguAbiProfileTool — ABI Profile v1 编码工具（Build-Time）

本工具由 CMake 的本机宿主构建生成，位于 `Build/<Platform>/Bin/PanguAbiProfileTool[.exe]`。依赖固定于 `Source/ThirdParty/NlohmannJson/Repo` submodule 的 nlohmann/json v3.12.0 正式发布版，构建前需初始化 submodule；仅包含工具代码和测试，不构成新的 Pangu Runtime Module。

从源码仓库根目录调用（示例里的 profile 必须来自未来可信 Toolchain Probe，**不要手工将示例 JSON 当作真实 Profile**）：

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

**安全边界**：工具只能核验声明的 Profile JSON **内容格式和 Digest**，当前**不核验**：Clang / CRT / STL / NDK / SDK 的实际来源、每个 TU 的 ABI Options、STATIC Catalog 汇合时的配置一致性、Module 二进制与预检材料的可信绑定及 TOCTOU、动态加载后核对。不可据此输出“已验证 ABI 安全”的结论。下一阶段需要在 CMake 工具链采集管道接入真实证据，失败时拒绝产生受信任 Fingerprint。

此工具刻意不接入 Android/iOS 交叉编译：它运行在构建宿主，不应被当作 Android/iOS 目标程序执行；交叉编译情形下需由宿主阶段生成受控的 Profile。

## 失败报告与文件状态

命令参数保持不变。失败时以非零退出码表示结果分类，stdout 输出一行独立的 JSON 失败报告，
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
