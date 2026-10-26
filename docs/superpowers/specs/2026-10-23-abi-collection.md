# 五平台 ABI Profile 采集器迭代设计

> 设计状态：Approved；实现状态：Completed（批准的 STATIC 范围，2026-10-27）。Task 1—12 的 68 项检查全部完成；R1—10 及提交 `9561fe6` 的追加 8 项评审问题已修复并独立复审通过，无剩余 Critical/Important。最近一次完整三桌面 CTest 为 **104/104**，移动端/CRT 构建与采集矩阵通过；追加修复与文档已按用户授权通过 amend 合入同一提交，未推送。见[计划与验收状态](../plans/2026-10-23-abi-collection.md)。最终 EXE/SHARED Loader 不在本轮范围。
> 日期：2026-10-23（设计）；实现状态核对：2026-10-27
> 实现基线：`dbfe7aa`（Windows 首切片）
> 权威协议：[ABI Profile / Fingerprint v1](../../../wiki/foundation/abi-profile-fingerprint.md)
> 关联：[构建系统](../../../wiki/foundation/build-system.md)、[工具链](../../../wiki/foundation/toolchain.md)、[ThirdParty](../../../wiki/foundation/third-party.md)

## 1. 意图与本轮范围

把已有 Windows 首切片迭代为统一的正式构建工具：采集流程由 `PanguAbiProfileTool` 拥有，CMake 提供真实 Target 与构建信息，五个平台采用同一使用契约。

用户已确认方案 B：统一采集器并补齐五个平台的现有 STATIC 场景；自动准备宿主工具，保留独立 Host 构建目录。普通 Module Target 构建成功时，库产物和配套 ABI Profile 必须同时有效；所有可达 C++ STATIC 依赖严格检查，PRIVATE 不豁免。本文确定本轮实现契约，内部实现细节交由实施者；设计确认不代表代码已经实现或协议全部冻结。

用户进一步明确：整理 `cmake/AbiProfile/` 内文件的归属与命名，尽可能以 C++ 实现功能逻辑；不能仅移动或重命名原有采集脚本。

本轮验收矩阵沿用现有开发环境：Windows x64、WSL Ubuntu Linux x64、macOS x64、Android ARM64、iOS ARM64。Android 其他 ABI、Apple Silicon 和 Simulator 不因 Schema 可表达而自动成为已验收组合；应在后续新增对应验证。

本轮完成全 TU 采集、真实依赖闭包检查、Profile/Canonical/Fingerprint 生成与构建失败处理。保留 v1 Schema、Canonical 字节协议及 Fingerprint 算法。

本轮包含库文件与 Profile 的构建内内容关联，用于完成状态和缓存有效性检查；不实现生产 SHARED Loader 的可信绑定、签名、TOCTOU 防护、热重载或设备部署。构建内摘要关联不等于 Loader 信任。Fingerprint 相同仍只是受控 C++ 互操作的必要条件。

## 2. 方案与取舍

| 方案 | 收益与成本 | 结论 |
| --- | --- | --- |
| A：收敛 Windows 实现，其他平台保留 pending | 改动较小；长期公共接口与跨平台来源核验仍需下一轮收敛 | 未选择 |
| B：统一 C++ 采集流程与平台后端，覆盖五平台现有 STATIC 构建 | 一次建立统一使用契约；需补齐宿主工具自举、进程执行、平台来源核验和依赖处理 | 用户选择 |

只搬迁 `.cmake` 文件不能完成职责调整。本轮迁移其采集行为与回归，避免保留两套独立的参数、来源与失败判断。

## 3. 普通 Module 的构建契约

**已确认的要求：采用强保证，普通 Module Target 构建成功时，库产物和配套 ABI Profile 必须同时有效。** 当前基线 `dbfe7aa` 尚未实现这一契约；这是方案 B 的必需交付与验收条件。

正常执行 `cmake --build --preset <platform> --target <ModuleTarget>` 后：

1. 命令成功意味着目标库及与本次构建对应的 Profile、Canonical JSON、Fingerprint 全部有效。
2. 必需的宿主工具、依赖 Target 和依赖检查自动完成，不要求调用者改用 `<Target>AbiProfile`。
3. 任一编译、归档、来源核验或采集步骤失败，命令失败，不能发布新的成功状态或沿用已失效的 Profile。
4. 关闭 `BUILD_TESTING` 不改变以上行为。测试目录仅增加回归目标和夹具。
5. 失败时允许磁盘仍存在库或诊断文件；不存在有效成功记录时，它们不能作为本轮已验证产物。不得把部分清理说成完整回滚。

为解决自举，Host 工具的私有 bootstrap 构建是明确例外：它只生成工具及其必要依赖，不向目标工程发布 Module 产物或 Profile；其 Foundation 不用于替代目标平台的 Foundation。

## 4. 职责与文件归属

| 层次 | 职责 |
| --- | --- |
| `cmake/AbiProfile/AbiProfile.cmake` | 平台无关接入入口；注册受管 Module、导出对象/依赖/配置、声明构建规则并调用工具 |
| `cmake/HostTools.cmake` | 如有必要独立拆分，负责 Host 工具配置、构建目录和构建依赖；不承担 ABI 采集与判定 |
| `Tools/AbiProfile/Private/` 公共采集实现 | 请求解析、命令行/响应文件处理、精确 TU 关联、进程执行、失效与清理、缓存与发布、统一失败报告 |
| 同目录中的平台后端 | 前端参数解释、对象/运行库检查、STL/SDK/Sysroot 来源核验、v1 字段映射与拒绝规则 |
| `Tools/AbiProfile/Probe/` | 正式采集使用的编译/链接输入；不是 Runtime Module，也不是临时 PoC |
| `Tests/AbiProfile/` | 真实采集与失败路径回归；调用正式工具，保留纯映射测试与最小夹具 |

根工程只使用统一入口，不再调用 `pangu_enable_windows_abi_profiles`。CMake 只描述构建关系并传递数据，不解析编译命令、运行库日志或 SDK Manifest，不进行 ABI 字段推断，不负责业务性缓存和输出提交。`cmake/AbiProfile/` 的四份旧文件迁移完成后全部撤销，不在原目录或工具目录保留另一套 `.cmake -P` 采集实现。

### 4.1 现有文件的处理与命名

| 现有文件 | 迁移后的职责与建议位置 |
| --- | --- |
| `cmake/AbiProfile/Windows.cmake` | 跨平台接线保留为 `cmake/AbiProfile/AbiProfile.cmake`；Host 自举接线按需要分入 `cmake/HostTools.cmake` |
| `cmake/AbiProfile/CollectWindows.cmake` | 公共流程迁入 `Tools/AbiProfile/Private/Collection.h/.cpp`；编译数据库及响应文件处理归入 `CompileCommands.h/.cpp`；Windows 特有行为进入平台后端 |
| `cmake/AbiProfile/WindowsRuntime.cmake` | 实际 SDK/CRT/STL 来源与链接证据核验归入 `Tools/AbiProfile/Private/WindowsCollector.h/.cpp`，复用现有 `WindowsProfile` 映射能力 |
| `cmake/AbiProfile/Prepare.cmake` | 删除脚本，失效/清理由 C++ 工具的统一入口实现，归入 `Collection` 的输出生命周期职责 |

以上名称表达职责，不要求机械地一文件对一文件翻译；相邻逻辑可按可维护性合并，不建立空壳适配层。
进程执行以工具私有接口表达，必要的 Host 差异可放在 `ProcessWindows.cpp` / `ProcessPosix.cpp`；不扩展为引擎通用进程框架。
平台后端采用明确名称，例如 `WindowsCollector`、`LinuxCollector`、`AppleCollector`、`AndroidCollector`；macOS/iOS 可共享 Apple 核验逻辑，按实际目标参数区分。

### 4.2 失效处理的先后顺序

独立 Host 工具已就绪后，由 CMake 调用 C++ 失效入口，再开始目标 Module 的编译，采集与最终发布也调用同一 C++ 工具。
Host 工具更新自身失败时，如需先撤销目标工程的旧完成标记，CMake 可使用自身内建文件命令撤销该已知标记；不解析 ABI 数据，不保留 `Prepare.cmake`，不声称工具已完成多文件清理。
完整产物清理、错误报告及代际判断仍由 C++ 实现；旧标记被撤销后，即使磁盘暂留旧 JSON/摘要也不构成本次有效结果。

沿用 `PanguAbiProfile` 实现库和 `PanguAbiProfileTool` 可执行程序，不增加 ABI 专用 Runtime Module、BinaryImage 层或统一 ThirdParty Wrapper。保留原有四参数编码接口；新增统一采集命令接收受版本控制的私有构建请求，内部完成采集、映射、校验与编码。旧 `--collect-windows` 映射入口可在回归迁移后移除或保留为明确的内部兼容入口，不能被描述为来源认证接口。

## 5. 宿主工具自举

所有平台统一使用独立的 `Build/Host/<HostPlatform>/`，避免 native 与 cross 场景采用不同的依赖模型。

- Host 阶段使用同一份仓库源码和独立 CMake 缓存，只构建工具及其必需 Foundation/JSON 依赖；不复制引擎源码。
- 默认构建 Release 宿主工具。目标平台的 Debug/Release、CRT、STL 与 ABI 配置通过请求独立传递，不能从工具自身的编译宏推断。
- 目标工程在任何 Module 编译/采集前确保 Host 工具已就绪；工具依赖 Host Foundation，不依赖目标 Foundation，消除环路。
- 工具就绪后执行 C++ 失效命令，替代现有编译前的 `Prepare.cmake`；首次自举不会再次依赖目标 Module。
- Host 的编译器/SDK 选择独立配置并验证，不能继承目标的 `CMAKE_TOOLCHAIN_FILE`、Sysroot、Android ABI 或 iOS SDK 参数。
- Windows 为 Windows/Android 构建本机工具；WSL Ubuntu 为 Linux 构建本机工具；macOS 为 macOS/iOS 构建本机工具。
- 源码、依赖版本、Host 配置或工具链发生变化时更新 Host 工具；正常增量构建复用它。先做正确的依赖跟踪，不建立远程二进制缓存或工具下载流程。
- 首次配置/构建自动完成自举；缺少 Host 编译环境时明确失败，不下载 SDK，不要求手工填写工具路径或 Fingerprint。

Host bootstrap 的最小工程可以复用现有 Target 定义，但不会执行目标工程的 ABI 完成规则。具体 CMake 分解由实施者选择；必须通过干净目录、单 Target、交叉编译及无循环依赖测试。

**Grill Q7 已确认**：同一宿主上的不同目标平台可以并行构建并安全复用 Host 工具缓存，例如 Windows/Android 或 macOS/iOS。
按源码、Host 配置和工具链身份区分工具版本；同一版本只允许一个构建者，成功发布的工具版本供使用者只读复用，不原地覆盖使用中的二进制。
允许保留少量版本化工具产物。实现可采用受锁保护的构建区和独立版本化发布区，不要求把 CMake 缓存目录移动或重定位；具体锁和目录细分属于构建接线实现。

## 6. 统一输入与全 TU 采集

私有请求描述实际 Host、Target、配置、选定编译器、对象列表、编译数据库、依赖与输出位置。它不是 Loader Manifest，不冻结为 Public ABI。

- CMake 在对应配置下导出真实对象集合，继续按规范化的 object output 精确匹配编译数据库；每个对象必须唯一匹配，不能按源文件选首项。
- 同源多 Target、生成源和条件源均按生成后的实际对象处理；无法解释的 C++ Modules、PCH、UNITY 或非 C++ 参与对象先明确拒绝，不猜测。
- 命令行解析取决于生成命令的 Host 语法，不取决于目标对象格式；例如 Windows 上 Android 命令仍按 Windows quoting 解析。
- 支持编译数据库的 `command` 与 `arguments` 表达，完整展开嵌套响应文件并限制大小、深度与循环。
- 通过显式 argv 启动已选定的工具，不拼接 Shell 命令；保存退出码、stdout、stderr 与操作阶段。
- 对每个 TU 验证有效宏、编译前端 Target、ABI 选项、实际 STL/SDK Header 来源和对象证据；同目标内不相容配置必须拒绝。
- 公共流程调度与输出固定；平台后端负责解释差异，不复制完整采集器。

**Grill Q5 已确认**：将实际影响所选编译器/链接器的有效环境纳入采集输入快照，例如参与选择的 INCLUDE、LIB、SDKROOT。
不保存或哈希整个进程环境，不把环境值直接加入冻结 Profile。相关环境发生漂移时失效旧结果，明确要求重新配置或重新验证，不复用旧 Profile。

**Grill Q1 已确认**：普通工程功能宏允许通过并完整记录到证据，不维持一个只接受少量普通 `-D` 的白名单。
编译器/平台 ABI 选项及 STL/CRT 等 ABI 控制宏仍需严格解释与验证，未知关键控制项拒绝。
普通宏引起的公共类型/API/ODR 差异属于相应 Contract 验收，不能把这些普通宏全部塞入冻结 v1 Profile，也不能声称 Profile 已覆盖所有宏导致的类型差异。

v1 无法表达的 STL ABI 配置保持拒绝策略。保留 Windows 已验证的 Header 配置限制；libc++ 同样核对 ABI namespace/version 及会改变布局的配置，不能只看 `_LIBCPP_VERSION`。

## 7. 平台后端

| 后端 | 来源与核验重点 | 执行边界 |
| --- | --- | --- |
| Windows | Clang、MSVC STL、COFF 指令、CRT 模式、实际选库、SDK Manifest 和 Toolset 安装元数据 | 保留现有 MD/MT 与 Debug 差异验证；不运行被采集库 |
| Linux | 实际目标 Triple、libc++ Header/ABI 配置、Sysroot 身份、glibc/musl 与实际 C++/ABI Runtime 选择 | 读取目标 Sysroot 元数据，不能把宿主发行版标签无条件当作目标身份 |
| macOS | MacPorts Clang、实际 libc++ Header、SDKSettings/SDK 路径、Mach-O/链接结果及部署 Target | 直接使用共享挂载源码；区分编译器安装与 SDK Runtime 来源 |
| Android | NDK 自带 Clang、目标 API、NDK libc++ namespace、`source.properties` 的 ReleaseName/Revision 与实际目标库 | 使用所选 NDK，禁止套用桌面 Clang23 限制；不运行 Android 程序 |
| iOS | MacPorts Clang、iPhoneOS SDK 元数据、部署版本、SDK libc++/libSystem 及目标对象 | 由 macOS Host 工具执行采集，不运行 iOS 程序 |

SDK/NDK 的完整安装元数据与实际文件身份保存到证据；Profile 仍按冻结 v1 的身份格式映射。本机 NDK 提供 `Pkg.ReleaseName=r30`，无需从包修订号猜测发布名。无法无歧义映射到 v1 的来源或关键配置必须拒绝，不能截断版本、填写 `unknown` 或偷偷扩展 Schema。

## 8. 真实依赖闭包

本轮必须支持受管 Module 对同一受控构建中其他 C++ STATIC Target 的依赖，替代基线 `dbfe7aa` 除 `PanguBuildOptions` 外一律拒绝的限制；该依赖闭包现已实现并验收。

- 以 CMake 当前配置实际使用的依赖为权威，解析别名和传递关系，保留链接顺序与必要的重复项；不能按名称排序或只读取一层 `LINK_LIBRARIES`。
- `INTERFACE` Target 不生成虚构库 Profile；其实际 Include、宏、选项与链接传播在消费者 TU/依赖闭包中检查。
- 同构建、源码可见的 C++ STATIC 依赖可生成内部证据与 Profile，不因此成为 Pangu Module，也不调用 `pangu_configure_module()` 改变其身份或警告策略。
- **已确认的要求**：闭包内所有可达、源码可见的 C++ STATIC 依赖执行 v1 严格相容检查。`PRIVATE` 不构成豁免依据；CMake 可见性不能证明是否存在 C++ 对象交换。本轮不新增“仅内部使用”的推断规则或绕过开关。不兼容或无法生成 v1 Profile 时拒绝，不通过全局参数强行改变上游配置。
- 对已核验的平台 CRT/SDK 系统库，使用平台后端的来源核验，不为其伪造 Module Profile。
- **Grill Q9 已确认**：本轮只接入共同受管采集的源码 C++ STATIC 依赖，普通预编译/IMPORTED C++ 库明确拒绝；附带 Profile 和文件摘要也不构成来源真实性证明，不能据此开放信任入口。预编译第三方的证据接入另行设计。
- 已核验的平台 CRT/SDK 运行库仍由平台后端单独处理；Host 工具的执行依赖不是目标平台的 C++ 链接依赖。SHARED 第三方、纯 C 库的独立 ABI 认证不在本轮验收范围，遇到未支持的边界给出具体诊断。

**Grill Q3 已确认**：STATIC 可以合法引用只由最终 EXE/Host 提供的符号，不要求每个静态库能独立链接成 EXE。
分别进行真实对象/归档的 ABI 与运行库引用检查、独立的最小运行库链接探针、未来最终消费者的实际链接检查。
最小探针按真实目标编译器及受控运行库政策构建且不执行，用于核验运行库选择；不能无条件加入全部 Module 对象，因缺少最终宿主符号而误拒绝合法归档。
必要依赖、归档顺序和重复项仍需如实保留；任何无法核验的 ABI 或运行库事实仍然拒绝，不能用“将来会链接”代替证据。

**Grill Q6 已确认**：本轮要求受管 Pangu Module 之间的 CMake 构建依赖无环；包含受管 Module 的构建依赖环明确拒绝。
普通技术 C++ STATIC 依赖内部的归档环仍支持，其强连通部分作为一组检查，不创建相互等待的 Profile Target 边。
这属于本轮构建支持范围，不能以运行期 Module 图的禁环规则作为依据：CMake Target/Link 图与运行期声明图并不等价。

该探针不是未来 EXE/DLL 的最终消费者。最终装配检查、Public Header ABI/ODR、物理库寿命和 SHARED 预检继续受各自规范约束，不能用本轮闭包检查替代。

## 9. 增量、失效与结果发布

每个 Target 按配置隔离输出，库与 Profile 有一个可检查的完成状态。仅存在旧的 `fingerprint.sha256` 不表示当前成功。

**Grill Q2 已确认**：完成记录关联实际库文件的内容摘要、Profile 摘要和采集输入快照。
即使路径未变，库内容被替换也不能继续沿用旧完成状态。关联信息属于构建内证据/BuildIdentity，不改变冻结 Profile 字段与 Fingerprint 算法，也不赋予签名或 Loader 信任。

**Grill Q4 已确认**：每次普通 Module 构建都执行轻量 Guard，检查完成记录与库/Profile 的内容摘要。
缓存有效时不重新运行 Clang 或链接探针；验证本身的进程启动和文件读取属于可接受的增量构建成本。

**Grill Q8 已确认**：编译器、SDK、ABI Header 或有效 ABI 编译选项发生变化时，必须重建受影响的目标对象并重新归档，然后重新采集。
优先由构建图自动完成；不能可靠触发所需重建时明确失败，指出需要重新配置/构建的原因。禁止用新环境生成的 Profile 重新标记旧环境编译出的对象。
库或对象出现与已记录构建不一致的内容变化时，也不能只刷新摘要来发布成功；必须先恢复可信的当前构建产物，或拒绝本次验证。

建议依赖集合至少包含请求/实际命令、响应文件、源与依赖 Header、真实对象、采集器、Schema、编译器及选定 SDK/Runtime 的身份材料。未能可靠追踪的来源，先重新检查，不能依赖不完整缓存获得成功结果。

- 已核验输入未变时复用有效结果；输入或对象身份变化时重新验证。
- 会使原结果失效的编译开始前撤销成功状态；上游编译失败也不能遗留可用的旧 Profile。
- 在私有临时位置生成全部输出，检查写入、flush、close 后完成发布；跨多文件发布使用最后完成记录指明同一代结果，不声称文件系统提供跨文件原子事务。
- 失败时逐项清理或撤销成功状态，保留可读错误及诊断文件；清理失败必须可见。
- 同一构建目录的并发采集使用互斥或代际隔离，避免正常库构建、显式工具调用和测试覆盖彼此输出。

Profile 中不加入绝对路径、时间戳或额外字段。路径、文件摘要、命令和完整 SDK/NDK Revision 等来源材料放在独立证据中，并继续声明 Loader 可信产物绑定/最终消费者验证的未完成范围。

## 10. 迁移与验收

先迁移并保持 Windows 已有行为和拒绝回归，再接入公共 Host/TU/输出流程及其余平台后端；最终以五个平台共同验收，不把中间 pending 状态作为方案 B 完成。所有正式采集与清理回归直接覆盖 C++ 工具入口；CMake 只测试生成的图、依赖和工具调用，不让测试继续依赖旧采集脚本。

验收至少包括：

1. 五平台干净构建、单 Module Target、依赖 Module Target、`BUILD_TESTING=OFF` 均自动得到有效结果。
2. 无变化增量构建能复用结果；源/Header/参数/响应文件/采集器/工具链或依赖变化会重验证。
3. 全 TU、同源多 Target、同配置依赖传播、合法静态依赖链与需要重复归档的链接场景正确。
   技术 STATIC 内部归档环正确；包含受管 Module 的构建依赖环明确拒绝，诊断不能混同运行期声明图。
4. 缺失/重复记录、未知 ABI 参数、第二 TU 冲突、STL Header 宏变异、SDK/Runtime 来源冲突均拒绝。
   普通功能宏应成功并出现在证据中；包含合法最终宿主未解析符号的静态归档也应成功完成采集；同路径库内容替换会失效旧完成记录。
   影响工具链的有效环境变化即使命令字符串未变也会失效旧结果；无变化构建执行 Guard 但不重复编译器/链接器探针。
5. 实际编译失败、采集失败、输出提交失败及清理失败不会发布假成功；旧结果不可误用。
6. Host 工具仅在 Host 运行；Android/iOS 的采集与链接探针从不执行目标程序。
   同 Host 的两个目标平台并行构建安全复用缓存，同一工具版本不会重复写入或覆盖使用中的二进制。
7. Windows 已有 Release MD、Debug MD、Release MT 回归保持；其余平台按实际 libc++/Runtime 配置补齐成功和拒绝用例。
8. 五平台 CMake 构建验证；三个桌面平台运行 CTest。设备运行、打包和签名另行验收。
9. 编译器/SDK/ABI Header/有效环境变化会触发必要重建或明确失败；不能仅给旧对象刷新 Profile。普通 IMPORTED C++ 库即使自带 Profile/摘要也会被明确拒绝。

具体 C++ 私有类型、进程封装、平台文件拆分、缓存算法和 CMake 图表达由实施者选择，并通过上述外部行为验证。新增 Public API、可复用通用进程框架或新的依赖管理层需要另行论证，不能借本次迁移顺带引入。

## 11. 可实施性检查与决策状态

独立 builder 检查已完成。随后 grilling 结合真实 STATIC 未解析符号与 CMake 归档环探针补齐了以下决策：

| 问题 | 已确认决定 |
| --- | --- |
| Q1 普通工程宏 | 允许并记录；ABI 控制项严格核验；公共类型/API/ODR 另有契约 |
| Q2 构建内内容关联 | 完成记录关联库内容、Profile 和输入快照，不等于 Loader 信任 |
| Q3 最终宿主提供符号 | 允许合法 STATIC 未解析符号；独立运行库探针不强行纳入全部 Module 对象 |
| Q4 无变化构建 | 每次执行轻量 Guard；缓存有效时复用编译器/链接器采集结果 |
| Q5 工具链环境 | 有效相关环境纳入快照；漂移后失效，不保存整个环境 |
| Q6 依赖环 | 受管 Module 构建图无环；普通技术 STATIC 内部归档环可分组处理 |
| Q7 并行 Host 缓存 | 按工具版本隔离，同版本单构建者，发布后只读复用 |
| Q8 ABI 输入漂移 | 必须完成必要重编译/归档或明确失败，不能给旧对象套新 Profile |
| Q9 预编译第三方 | 普通 IMPORTED C++ 库本轮拒绝，不以自带 Profile/摘要代替来源核验 |

采用全部严格检查，暂不提供内部 C++ 依赖豁免。代价是 ABI 配置不相容的源码第三方会被明确拒绝，即使使用方将其标为 PRIVATE；这是限制本轮支持边界，不是授权修改第三方构建配置。将来若需要允许不相容库，应先设计可验证的隔离边界，再决定认证方式。

Q1—Q9 均已确认。更新后的设计已通过独立决策闭合检查：没有残留矛盾或新的重要用户决策，问题前沿为空。
用户已最终确认整份共同理解。本文契约对应的 STATIC 实现、独立复审和五平台验收现已完成，见[计划与验收状态](../plans/2026-10-23-abi-collection.md)。实现主体原提交为 `9561fe6`，追加评审修复与文档已按用户授权通过 amend 合入同一提交，未推送；最终消费者和 SHARED Loader 继续单独设计与验收。
