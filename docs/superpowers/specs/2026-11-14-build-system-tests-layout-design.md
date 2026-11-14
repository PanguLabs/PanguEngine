# 构建系统测试目录命名

状态：用户已确认并已实施；验收范围见[实施计划](../plans/2026-11-14-build-system-tests-layout.md)。

## 目的与范围

构建系统测试按被测职责命名，替代按实现工具命名的 `Tests/CMake`。用户确认的迁移为：

| 旧路径 | 新路径 |
| --- | --- |
| `Tests/CMake` | `Tests/BuildSystem` |
| `Tests/CMake/SourcePlugins` | `Tests/BuildSystem/PluginIntegration` |
| `Tests/CMake/SourcePluginProject` | `Tests/BuildSystem/ProjectIntegration` |
| `Tests/CMake/Testing` | `Tests/BuildSystem/TestInfrastructure` |

其余子目录保留名称和层级。迁移全部正式源码与夹具，保留测试名称、标签、运行层次、超时、处理器预算、资源锁和断言；不增加测试框架，不拆分测试注册职责。

## 方案取舍

保留 CMake 名称不足以表达插件、Project 与测试框架的集成范围；只改根名也不能解决三个子目录含义不清的问题。按领域彻底拆散会扩大共享夹具迁移范围。本次采用用户确认的四处目录重命名，其他结构保持稳定。

## 引用和文档

同步原生 CMake、构建脚本、相对 include、夹具引用、Git 忽略例外及当前规范文档。历史规格与验收中的原始命令保留当时路径，修正其中指向现行源码的 Markdown 链接，并通过本规格和目录 README 给出迁移对应关系。

## 验证与交付

迁移前后比较 CTest 的名称、标签、运行属性与命令参数；仅允许源码路径发生对应替换。核对文件数量与内容归一化一致，禁止丢失覆盖。重新配置、定向构建受影响 C++ 夹具，运行已有构建系统回归和脚本边界测试；涉及 Project Git 锁的验证必须明确源码状态及 override 条件。

本机优先在当前共享源码工作区实施，各宿主 BuildRoot 及测试工作目录政策保持不变。不自动清理其他任务缓存、不推送远端。
