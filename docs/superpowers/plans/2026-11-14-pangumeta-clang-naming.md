# PanguMetaTool Clang 自检命名清理计划

> **For agentic workers:** 使用 superpowers:executing-plans 在现有共享源码分支逐项执行，结尾一次独立评审。

**Goal:** 去掉自定义 SDK 术语及过渡代码，保留真实 Clang 自检。

**Architecture:** 同一普通工具工程，仅重命名内部文件/接口、CLI、测试组、诊断与当前文档；删除退役缓存迁移，不改变生成与依赖图语义。

**Tech Stack:** C++23、CMake 4、Clang 23、Ninja、GoogleTest。

**Spec:** [设计](../specs/2026-11-14-pangumeta-clang-naming-design.md)

## Global Constraints

- 不改变 Engine/生成代码 API、协议和 ABI，保持标准库/RTTI/CRT 政策。
- 新 CLI --check-clang；旧 --check-sdk 返回用法错误，不保留兼容别名。
- 保留普通缓存来源保护、失败传播与 no-op，取消旧 SDK 身份自动迁移。
- 保留真实系统 SDK、上游变量及历史验收原文，不做全仓库 sdk 替换。
- 共享源码与各平台 BuildRoot 不变，不下载/构建 LLVM，不使用包管理器。

## Review Focus

- C++ 类型、文件和构建宏必须同时更名，RTTI 报告语义不变。
- CLI 的 Windows/Unix 分支、构建后检查及移动调用必须同步。
- 测试目标/组/正则及压缩夹具路径必须一致。
- 过渡代码移除不能顺带移除普通缓存来源和错误产物保护。
- 当前文档去掉自制 SDK 混淆，历史记录和系统 SDK 名称保留。

### Task 1: 命名与过渡代码

- [x] 增加旧 CLI 被拒绝的断言，观察基线 RED。
- [x] 重命名 C++/CLI/测试/诊断，移除旧缓存迁移；同步所有操作性调用。
- [x] 通过三桌面工具构建与完整测试。

### Task 2: 规范、评审与收尾

- [x] 更新当前文档与接口名，保留历史原文。
- [x] 验证正式宿主入口、格式、针对性 clang-tidy；独立评审。
- [x] 归档准确验收结果，确认任务退出后清理本计划临时目录并提交。

## 执行记录

基线 f110a0d，用户已批准清单，沿用共享源码与当前功能分支，不再重复请求方案确认。

### 实施摘要

Driver/SdkInfo.h/.cpp 已更名为 ClangInfo，入口改为 CheckClang() 和 --check-clang；内部 CheckConsumer/CheckAction/CheckValue/CheckOptions、ClangCheck.cpp 及 CLANG_HAS_RTTI 宏同步。测试文件/目标/组统一为 ClangLinkTests.cpp、PanguMetaClangTests 和 MetaClang，压缩夹具移至 Fixtures/Dependencies，诊断统一为 toolchain_error / stdlib_mismatch。自检逻辑、生成协议、标准库/RTTI/CRT、正常依赖图不变。

删除根目录与工具 CMake 的旧参数缓存清理、宿主脚本的旧 SDK 身份自动迁移。普通来源/编译器/LLVM_ROOT 身份不匹配仍拒绝，锁和失败产物清除保留；新增 RejectsLegacyIdentity 验证不会再自动接受旧身份。旧 CLI 不保留别名。

当前说明改为 LLVM/Clang 开发包或库，更新当前移动说明中的安装版前端信息。真实 DIA/Windows/Android/Apple SDK 及上游变量保留，历史记录不机械替换；ADR-0019 和上轮规格仅增加当前命名说明，保留其决策原文。

### 验收（2026-11-14）

| 宿主 | 独立工具完整 CTest | 正式宿主入口 |
| --- | --- | --- |
| Windows / LLVM 23.1.5 | 74/74，通过 | windows preset + PanguMetaToolHost + --check-clang 通过；旧 CLI 用法拒绝复验通过 |
| WSL Ubuntu / LLVM 23.1.0 | 75/75，通过 | linux preset + PanguMetaToolHost + --check-clang 通过 |
| macOS / MacPorts 23.1.2 | 75/75，通过 | macos preset + PanguMetaToolHost + --check-clang 通过 |

独立完整测试复用各宿主 BuildRoot/Tests/BuildTools/PanguMetaNormalBuild：`cmake --build <cache> --parallel 4` 后执行 `ctest --test-dir <cache> --output-on-failure --no-tests=error --parallel 4`。新分组、目标名、正则、头文件及宏均通过实际编译/运行验证，原有属性/IR/上下文/生成/发布用例保留。

RED：修改 CliUsageTests.cmake 增加旧 CLI 拒绝断言后，旧工具返回 0，使测试失败。改名后该断言要求返回 2 且 stderr 有 Usage，新命令的实际检查成功。新增旧身份拒绝测试通过，不建立 CMakeCache。

重命名过程中发现历史文件负向断言被机械改名，已恢复真正退役的 PanguMetaSdk.cmake 名称。旧标识扫描仅命中刻意保留的旧 CLI 拒绝断言和历史文件负向断言；它们不作为操作性 API。真实 SDK 的说明、通用第三方术语和系统变量不删除。

静态验证：ClangInfo.cpp/.h、Main.cpp、ClangLinkTests.cpp 和搬迁 C 夹具通过 clang-format；三个变更 C++ TU（ClangInfo.cpp、Main.cpp、ClangLinkTests.cpp）使用实际 Windows 普通工具编译数据库通过仓库 clang-tidy，无需规范化模块响应文件。git diff --check 通过。后续仅 CMake 局部变量名和文档整理，再用正式 Windows 入口和 CLI 用法定向复验。

一次独立只读评审核对全部重命名、自检、调用、身份保护和文档范围，未发现新的实质问题。未重复完整 Engine、移动或双模式矩阵；上一轮移动结果保持其原提交范围。

### 收尾

所有源码、测试、规格与必要结论纳入提交；不推送或合并。仅清理本计划 `.superpowers/sdd/2026-11-14-pangumeta-clang-naming/` 临时目录，保留正式测试与普通构建缓存。
