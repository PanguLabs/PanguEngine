# PanguMetaTool 普通工具构建

> 自检接口名称已由[Clang 命名清理设计](2026-11-14-pangumeta-clang-naming-design.md)更新为 --check-clang、ClangInfo/CheckClang；本文保留原决策时的名称，普通构建架构不变。

状态：实施、独立评审与三桌面工具/原生 Meta 接线验收完成；基线 `00a9607`。Android/iOS ARM64 Monolithic Meta 最终链接定向验证通过，不代表设备运行或完整矩阵。

## 目标

取消独立 LLVM SDK 准备层。新电脑只安装完整 Clang 23 开发包、初始化固定 submodule，再使用正常 CMake 构建入口。删除源码 `BuildTools/PanguMetaTool/Sdk` 及宿主 BuildRoot 的 `Sdk/PanguMeta`，不再生成或消费 PanguMetaSdk.cmake，不执行 Prepare/Inspect/Check 独立工程，不构建 LLVM、不使用 vcpkg。

## 结构与契约

独立 PanguMetaTool 直接从环境 LLVM_ROOT 选择宿主编译器、LLVM/Clang CMake 包及资源头。Windows/MSVC STL、Linux/libstdc++、macOS/libc++ 的现有工具政策不变；Engine 与生成代码保持自己的目标配置。直接继承 Clang 的目标匹配 LLVM_ENABLE_RTTI，其他工具代码保留 RTTI。

zlib/zstd 为同一工具工程内的普通私有静态目标，使用现有固定 submodule 和 ThirdParty 适配。仅为 LLVM 包导出提供常规 Find 模块与目标别名，不生成安装前缀或另一套依赖配置。读取所选 LLVM 导出元数据后，对已有依赖目标设置匹配的 CRT；DIA SDK 通过显式系统变量、VS 环境或 vswhere 发现。

配置阶段验证安装路径、版本、宿主标准库及资源头。实际 LLVM 链接由普通目标构建验证；工具链接后执行 --check-sdk 验证属性注册、AST 和 constexpr 求值。失败删除该次构建的工具可执行文件并使构建失败，后续构建必须重试。SDK 输入、资源头、依赖源码与 CMake 接线纳入已有宿主工具输入清单，不增加日常无变化扫描。

Engine 的原生宿主工具入口只传来源、宿主构建目录与 Engine 路径；资源路径由普通工具配置生成的清单提供。源/编译器身份保护保留；相同源与编译器的旧 SDK 配置缓存可一次性迁移。Meta 任务必须有可用工具，纯登记且无任务不得启动工具。测试与移动交叉解析不依赖已删的 SDK 文件。

## 测试迁移与删除

保留所有 Attr/IR/Request/Context/CodeGen/Publish/Cli 有效覆盖。移除独立 Prepare 的目录/候选发布等已废弃契约测试，替换为自动发现、缺失开发包/资源、错误标准库、固定源码版本、压缩往返、构建后验证失败与重试、普通 Ninja 增量测试。三桌面工具全套与 Engine 原生 Meta 接线定向验证通过后再删除旧宿主 SDK 产物，更新规范并保留历史规格的替代说明。
