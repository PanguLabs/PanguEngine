# PanguMetaTool Clang 自检命名清理

状态：实施、独立只读评审和三桌面完整工具测试完成；基线 f110a0d。

将自定义 SDK 命名改为实际职责：Driver/ClangInfo.h/.cpp、ClangInfo、CheckClang()、--check-clang；内部探针使用 CheckConsumer/CheckAction/CheckValue/CheckOptions 与 ClangCheck.cpp。RTTI 宏改为 CLANG_HAS_RTTI，测试使用 ClangLinkTests.cpp、PanguMetaClangTests、MetaClang.*，压缩夹具移至 Fixtures/Dependencies。

诊断统一为 toolchain_error、stdlib_mismatch。删除已迁移完毕的旧 SDK 缓存参数清理与旧身份自动迁移，但保留普通源/编译器/LLVM_ROOT 身份检查、锁、失败产物清除及真实 Clang 验证。旧 --check-sdk 不保留别名，需作为用法错误拒绝；仓库内所有调用同步更新。

只改名称和退役过渡代码，不改变属性注册、AST/constexpr 检查、生成协议、标准库/RTTI/CRT、原生依赖和正常 no-op。当前文档改用 LLVM/Clang 开发包或库；DIA SDK、Windows SDK、Android/iPhoneOS/macOS SDK 与上游变量 MSVC_DIA_SDK_DIR 保留，历史规格/ADR/验收保留原文并沿用替代标识。

验证旧 CLI 拒绝、新 CLI 接纳、三桌面独立工具全套与原生宿主入口；进行针对性格式/静态检查及一次独立评审。不重跑未改动的完整 Engine/移动/双模式矩阵。
