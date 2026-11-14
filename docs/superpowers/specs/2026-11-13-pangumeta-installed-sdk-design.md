# PanguMetaTool 复用已安装 LLVM SDK

> Superseded（独立 SDK 准备与配置部分）：现行 [普通工具构建设计](2026-11-14-pangumeta-normal-build-design.md) 已取消该流程；保留原规格与历史验收边界。
状态：用户已选择方案 1；实现、独立评审与三桌面工具验收完成。完整 Engine/移动端矩阵未重跑，结果见对应实施计划。

## 目标与约束

Windows/Linux 不再由项目编译 LLVM。PanguMetaTool 直接消费已安装的 Clang 23 开发包，保留现有 pga 属性语法、原始源码解析、常量求值、IR、代码生成和失败契约。目标引擎仍使用现有标准库和 RTTI，不将宿主配置传播到生成代码或目标编译上下文。

用户选择整体工具匹配 SDK：Windows 为 MSVC STL；Linux 允许 PanguMetaTool 独立工程使用 libstdc++；macOS 维持 libc++。Clang C++ 接口编译单元按 SDK 的 RTTI 开关构建，其余自有代码和 GoogleTest 保留 RTTI。此例外仅属于独立宿主工具及其测试，不修改 Engine 通用工具链。

## 方案比较

1. 整体工具匹配现成 SDK（已选）：改动最少，允许宿主工具标准库和 Clang 适配代码 RTTI 的局部例外。
2. 独立 Clang 前端进程：主程序可维持原政策，但增加程序、序列化协议和部署成本。
3. libclang C API：不能直接覆盖现有自定义属性注册和 Sema/APValue 语义，不作为等价替换。

## 实现

独立工程根据宿主选择 SDK 对应标准库，通过真实编译/链接检查 string ABI。LLVM_ENABLE_RTTI 决定接触 Clang 类层次的目标配置，不再要求 ON。SDK 检查直接登记 Type/Field，解析带属性和 constexpr Options 的原始 TU；无效属性必须失败。SdkInfo 如实报告 SDK RTTI 和宿主标准库。

保留显式 Check.cmake 接入入口；Prepare.cmake 改为仅检查已安装开发包，拒绝旧源码构建参数，不下载、构建或覆盖 LLVM。已有 SDK 构建产物保留，不擅自删除。SDK 配置缓存切换必须重新验证。

## 验收

Windows/Linux 使用现有安装的 SDK 配置并构建独立工具及全部 PanguMetaTool 测试；覆盖无 RTTI SDK、真实 ABI 错配、资源缺失、自定义属性与拼错属性、生成消费。Linux 生成消费仍使用 Engine libc++ 工具链。macOS 至少验证现有 SDK 的配置和受影响检查，可用时运行相同测试。运行仓库格式与针对性 clang-tidy；明确未验证边界。
