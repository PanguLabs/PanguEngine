# ADR-0012：共享源码、构建树按宿主本地文件系统隔离

> Status: Accepted
> 日期：2026-10-25
> 权威规范：[Pangu 构建系统](../foundation/build-system.md)

## Context

Windows 是 PanguEngine 唯一源码工作区；WSL Ubuntu 和 macOS 通过 SMB 挂载直接访问相同源码。先前全部平台使用仓库内 `Build/<Platform>`，导致 Linux/macOS/iOS 构建目录位于 Windows SMB 共享上。ABI Profile 采集和 CTest 已实测 CIFS/SMB 文件身份、大小写别名、硬链接与符号链接语义差异；构建树包含高频的小文件 I/O、对象、缓存、锁和完成记录，不适合长期共用 SMB 文件系统。

## Decision

1. **源码统一共享，构建输出按 Host 本地化**；CMake Preset 的 `binaryDir` 是正式构建根，SourceRoot 与 BinaryDir 不必在同一文件系统，也不建立兼容性伪装挂载。
2. Windows Host 的 Windows 与 Android Target：保留 Windows NTFS 源码仓库下的 `Build/Windows`、`Build/Android`。
3. WSL Ubuntu Host 的 Linux Target：使用 ext4 的 `$HOME/PanguEngineBuild/Linux`。
4. macOS Host 的 macOS 与 iOS Target：使用 APFS 的 `$HOME/PanguEngineBuild/macOS`、`$HOME/PanguEngineBuild/iOS`。
5. Linux/macOS/iOS Preset 通过 `$env{HOME}` 定位构建树。继续从既有 SMB 挂载读取**同一份源码**，不复制仓库。不同 Host/Target 不共用可变 CMake Cache、对象文件、编译数据库、ABI 收据、锁或生成源码。
6. 历史的 `$HOME/PanguBuild/PanguEngine/{Linux,macOS,iOS}` 目录及更早仓库内的 `Build/Linux`、`Build/macOS`、`Build/iOS` 均保留；新目录须 fresh 配置与构建，不迁移旧 CMake Cache 或完成记录，不自动清除旧目录。需要分发时显式导出选定 Artifacts。

## Alternatives

- 全平台继续使用 Windows SMB：方便在 Windows 集中查看，但 构建工具、Ninja、CTest 和 ABI 事务需要处理非原生文件身份与元数据语义。
- Linux/macOS 各自复制一份源码：构建 I/O 可以全部本地化，但产生多个需要同步的工作区，违背单一源码原则。
- 将 ext4/APFS 反向挂载为仓库下的 `Build/<Platform>`：会使不同 Host 对相同路径观察到不同文件，故不采用。

## Consequences

主机本地构建可避开构建输出侧的 SMB 语义差异，不能消除共享源码读取的网络开销；ABI 工具仍必须进行内容校验和 Fail Closed。macOS/iOS 的双平台回归应从本地构建根定位各自的 `compile_commands.json` 和测试证据。WSL 与 Mac 本地构建占用空间需要管理和备份；现有历史文档的旧构建位置只代表对应日期的测试证据。

## Open Questions

构建缓存淘汰与 Artifact Export、未来不同 Host 的 Android 交叉编译、CI 多工作区与构建目录定制策略在后续另行设计。
