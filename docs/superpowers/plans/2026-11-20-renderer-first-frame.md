# Renderer 五平台首帧 Implementation Plan

> **状态：已被替代（Superseded），原方案整体验收未完成。** 原接入路线停止于 `18541d2`。后续实施以 [Renderer 原版 Forge IGraphics 迁移实施计划](2026-11-20-renderer-forge-igraphics.md) 为准。
>
> **承接方案已验收。** 2026-11-24，macOS 和 iOS 通过编译及最终链接，Windows、Android 和 Linux 通过约定范围内的运行补验。结果已在 `98ef4fa` 归档，Vulkan 正式发布缺项继续按接入规格跟踪。[各平台补验与结项](2026-11-20-renderer-forge-igraphics.md#2026-11-24-各平台补充验证)

下方原任务清单供追溯。实际进度见文末的收尾记录。

**Goal:** 用同一份 Project 业务源码完成五平台清屏、窗口生命周期与安全退出，并交付能独立部署的无图形工具。
**Architecture:** Engine 按可执行目标组装 Application。图形入口持有 EngineHost 和平台窗口，用 Host Scope 的 Renderer 借用管理设备与交换链；无图形入口只组装已有非图形能力。第三方原生代码、兼容性补丁与平台打包各有独立边界。
**Tech Stack:** CMake 4.0+、Ninja、C++23 / Clang、GoogleTest / CTest、The Forge v1.63、D3D12、Vulkan、Metal。
**Spec:** [原首帧规格](../specs/2026-11-20-renderer-first-frame-design.md)。实施基线 `735aea3`。状态：历史计划。

## Global Constraints

- gitlink 固定 `7443530ef7e888771f307de74b7872becfd64450`。正式构建和验收的输入全部纳入版本控制，配置期离线。`.probe/forge-first-frame` 仅供开发者核对已有实验。
- Project 只调用 Engine 接入接口，同一目标切换 Monolithic / Modular 时保持相同源码与 CMake 接线。无图形工具在缺少 Renderer 的环境启动 EngineHost、执行操作并关闭，无需 NullRenderer。
- Windows/D3D12、Linux/Xlib + Vulkan、macOS/Metal、Android arm64/Vulkan 均双模式；iOS arm64/Metal 只要求 Monolithic。每个组合至少 120 次后端成功接纳的呈现请求，错误与跳帧单列。每平台至少一种模式跑完整窗口生命周期。
- 每进程一个图形会话，每会话一个有效 Surface、一条调用线程。设备与 Surface 的寿命独立。使用 `pg::Result<T>`、`pg::Status`、拥有型失败现场与现有 Module/Scope 生命周期。
- 按 [项目规范](../../../AGENTS.md)、[C++ 规范](../../../wiki/foundation/cpp-guidelines.md)、[CMake 命名](../../../wiki/foundation/cmake-naming.md)和 [Tests 维护规则](../../../Tests/README.md)执行。仅真实 Runtime Module 定义 `Pangu::` 别名。新增 CMakeLists 以 4.0 开头，在所属工具链上下文校验 Clang。
- Windows/Android 从 NTFS 工作树运行正式 preset；Linux 使用 WSL Ubuntu 的 `/home/moozen/WindowsGit/PanguEngine` 和 ext4 BuildRoot；Mac 经 `ssh -o BatchMode=yes moozen-macos` 在 `/Users/moozen/WindowsGit/PanguEngine` 共享源码上构建，macOS/iOS 分用 APFS BuildRoot。同一平台的正式 BuildRoot 串行切换模式。

## Review Focus

| 容易遗漏的情形 | 必须观察到的行为 | 任务 |
| --- | --- | --- |
| 取图尚未返回就收到销毁通知，下一次挂接又复用句柄值 | 撤销得到处理，迟到的 A 通知不会拆掉 B | 2、5、7 |
| 提交失败前 Fence 已重置，或最后一次等待才报告设备丢失 | 只等待成功提交关联的完成条件，首次失效阶段可追踪，同一宿主可重开 | 2、4–6 |
| 渲染 Fence 已完成，呈现队列仍在使用等待信号量 | resize 和 stop 均推迟回收旧交换链与呈现同步对象 | 2、5 |
| 最后一帧之后才到达 Metal 错误，或 Android 在同一进程再次打开 Activity | 关闭返回异步失败；Activity 销毁后再次启动取得全新会话 | 6、7 |
| 开发机搜索路径补齐漏打包文件，或旧像素一直留在屏幕 | 隔离部署能定位真实依赖；恢复或换色后采到新帧 | 1、8、9 |

---

## 文件与职责

下列文件名及新增接口是本计划的实现选择。目录前缀在各任务中沿用。

| 工作单元 | 新建或修改的主要文件 |
| --- | --- |
| 消费入口 | 新建 `cmake/PanguApplication.cmake`；修改 `cmake/FindPanguEngine.cmake`、根 `CMakeLists.txt`。新建 `Source/Application/Public/PanguApplication.h`、`Source/Application/Private/HeadlessMain.cpp`、`GraphicsApplication.cpp`（后者同属 Private）。Application 接线编入最终应用目标。 |
| Renderer | 新建 `Source/Runtime/Renderer/CMakeLists.txt`、`Public/{RendererModule,RendererAccess,RendererSession,RendererTypes}.h`、`Private/{RendererModule,RendererAccess,RendererSession}.cpp`、`Private/{FrameBackend,NativeSurface}.h`、`Private/Backends/{ForgeBackend.cpp,D3D12.cpp,Vulkan.cpp,Metal.mm}`。共享的状态机与各后端通过私有接口连接。 |
| 依赖与平台入口 | 新建 `Source/ThirdParty/TheForge/{Repo,CMakeLists.txt,README.md,Patch/}` 和 `Source/Application/Private/Platform/{Win32.cpp,Xlib.cpp,MacApplication.mm,AndroidApplication.cpp,IosApplication.mm}`；修改 `.gitmodules`。Patch 作用于 BuildRoot 内的受控源文件副本。 |
| 测试与部署 | 新建 `Tests/Renderer/{CMakeLists.txt,README.md,SessionTests.cpp,HostTests.cpp,BackendTests.cpp}`、`Tests/Renderer/Project/{CMakeLists.txt,CheckContext.cpp,FirstFrame.cpp}`、`Tests/Renderer/Acceptance/` 下的构建、运行及核验文件。修改 `Tests/CMakeLists.txt`；复用 `cmake/PanguRuntimeDeployment.cmake` 及现有 Project 夹具的工具链和 Git 锁检查。 |
| 交付说明 | 更新 `wiki/runtime/README.md`，新建 `wiki/runtime/renderer.md`，在本计划末记录任务状态、实际测试提交、九组结果和遗留事项。正式测试长期保留。 |

**统一验证记法。** 下文 `check(Target, Regex)` 代表先执行 `cmake --build --preset windows --target Target`，再执行 `ctest --preset windows --no-tests=error --output-on-failure -R Regex`；其他桌面替换为相应 preset。每次 RED 要求因目标行为未实现而失败，GREEN 要求相同断言通过。新增测试先接线并重新 configure；以 `pangu_register_gtest` / `pangu_register_test` 指定 area、boundary、tier 和 build target。各任务最后只暂存本任务文件，使用所列中文提交标题。任务 4–8 的设备验收入口在各自任务落地，可单独运行。

**任务间接线。** 任务 1 即建立独立消费夹具和 `Acceptance/BuildProject.cmake`，供任务 4–8 增量扩展；任务 9 复用这个入口。任务 2 的无设备测试和任务 4 的 Host 测试进入正式 CTest。真实桌面 GPU 验收通过显式配置 `-DPANGU_TEST_RENDERER_DEVICE=ON` 注册 `Renderer.DesktopFrame`（system/full），由宿主 CTest 驱动；普通无显示构建运行可控后端测试。移动端使用桌面宿主设备脚本，保持现有交叉构建的目标 GTest 跳过逻辑。

### 任务 1 · Project 接入 Engine，交付最小无图形工具

**文件与接口。** 创建文件表的消费入口、`Tests/Renderer/Project/{CMakeLists.txt,CheckContext.cpp}`、`Tests/Renderer/Acceptance/Headless.cmake` 与测试接线。新增 Engine 函数 `pangu_add_application(TARGET <name> KIND HEADLESS SOURCES <files...>)`；由 Engine 提供 main，Project 定义 `pg::Status pg::app::RunTool(pg::engine::EngineHost&) noexcept`。Project 继续先调用 `find_package(PanguEngine REQUIRED MODULE)` 和 `pangu_initialize()`。夹具目标名为 `CheckContext`。在目标上私有链接 `PanguBuildOptions` 和 `Pangu::EngineHost`，保留引擎与 Project 产物分根。

- [ ] 写独立 Project 系统测试 `Renderer.Headless`。夹具在 RunTool 内创建并关闭一个真实 Context，成功才发出 `context-closed`，Engine 成功停止后退出 0。核心断言为 `exitCode == 0 && sawContextClosed && rendererImports.empty()`。新目标须可与现有 Project 目标并存。
- [ ] 在 Windows Modular configure 后执行 `ctest --preset windows --no-tests=error -R '^Renderer\.Headless$' --output-on-failure`，观察缺少接入函数或工具无法完成契约。
- [ ] 实现启动、调用与停止，复用 `EngineHost::Start/BeginShutdown/TryFinishShutdown`。按图形能力在目标上接线；仅构建无图形目标时不得有强制窗口 SDK 查找或 Renderer 装载边。复用 ProjectIntegration 的独立构建及锁校验方法。在新夹具的 `Tests/Renderer/Project/CMakeLists.txt` 启用 C/CXX，消费源码副本设置独立 EngineBuildRoot；由宿主驱动将当前干净提交写入该副本的锁文件，开发阶段显式使用 override，避免提交自引用的 Git 锁。
- [ ] 重跑同一测试，在只含工具与其非图形依赖的可搬迁目录中运行，检查引擎操作和关闭结果。提交 `feat: 增加面向 Project 的无图形应用入口`。

**现有接线位置。** 根 `CMakeLists.txt:63` 的 owned-subdirectory 列表管理运行时目标；`cmake/FindPanguEngine.cmake:47` 负责独立 Project 初始化；`Tests/CMakeLists.txt:8` 在交叉编译时提前返回。桌面测试按 `Tests/ECS/CMakeLists.txt` 的 GoogleTest 注册与 staging 方式接入；移动目标走独立 Project 构建，设备运行由宿主驱动。

### 任务 2 · 可验证的图形会话与退休顺序

**文件与接口。** 创建 RendererTypes、RendererSession、NativeSurface、FrameBackend 和 SessionTests。`LinearColor{float R,G,B,A}`、`PixelSize{uint32_t Width,Height}`、强类型 `SurfaceId`，`FrameResult` 的正常值为 `kPresented/kSkipped/kReattach`。错误用 Renderer 域和 `Open/Attach/Acquire/Submit/Present/Wait/Drain` 阶段区分，设备丢失作为错误上报。`RendererSession` 提供 `Status Open()`、`Status Attach(SurfaceId, NativeSurface, PixelSize)`、`Status Detach(SurfaceId)`、`Result<FrameResult> Present(LinearColor)`、`Status Close()`，均 noexcept。NativeSurface 是平台适配器构造的借用，具体类型限 Engine 内部使用。会话内选择生产后端，单测通过私有构造注入可控后端。错误域用 `ErrorDomainId::Constant<0x70672e72656e64ULL>()`；`Error::Code()` 编码失败阶段与原因，`NativeErrorCode` 保留 HRESULT、VkResult 或 Metal 原生码。

- [ ] 在 `SessionTests.cpp` 用可控后端记录原生调用顺序、提交状态和两种退休完成条件。测试 `BusyAndRollback`、`StaleDetach`、`AcquireInterrupted`、`SubmitFailure`、`PresentRetirement`、`LossDuringDrain`。关键断言如 `EXPECT_EQ(backend.WaitOnUnsubmittedFenceCount, 0U)`、`EXPECT_EQ(backend.DestroyedSwapchains, 0U)`（仅渲染完成时）、`EXPECT_EQ(backend.LastPresentedSurfaceId, idB)`（测试观察器）。模拟两个 Host 并发 Open、一侧初始化中途失败后重试、零尺寸、重复 Close 和同一原生句柄复用。
- [ ] `check(PanguRendererTests, 'PanguRendererTests.Session')` 观察 RED。私有状态机可先作为测试和后续 Module 共用的内部目标编译，禁止把测试后端作为首帧产品后备路径。
- [ ] 实现进程级独占，从占用预留到初始化或回滚都不可被并发穿透；SurfaceId 由 Application 为每次挂接分配新代次。私有后端提供 `Status Open()`、`Status Attach(NativeSurface, PixelSize)`、`Result<AcquireResult> Acquire()`、`Status Submit(LinearColor)`、`Result<FrameResult> PresentImage()`、`Status RetireSurface()`、`Status Close()`。`AcquireResult` 取 `kReady/kSkipped/kReattach`。每会话串行操作，后端持有当前获取的图像和在途记录。由生产状态机决定 Acquire 成功后是否 Submit、何时 PresentImage；测试替换原生操作并检查实际调用历史。BackendTests 另外针对真实后端适配逻辑注入原生 API 结果，验证 Fence 与呈现对象回收。撤销标志可跨线程发布，原生资源操作始终在会话线程；每次有界等待返回时先处理撤销。Close 返回导致关闭的异步首错，同时保留完成清理的责任直至安全退休。
- [ ] 逐阶段注入失败，分别模拟提交前失败、提交成功后呈现失败、等待或关闭首次发现丢失。验证 `presentAfterFailedSubmit == 0`、`reopenAfterRetirement == success`，重复运行 GREEN。提交 `feat: 实现 Renderer 会话及 Surface 退休契约`。

**状态机测试接口。** 在 `Tests/Renderer/FakeFrameBackend.h` 提供可按阶段注入原生结果和独立推进渲染/呈现完成的测试后端。检查正在挂接的代次从测试后端的事件历史读取。会话线程外只允许发布撤销请求：`void RequestDetach(SurfaceId) noexcept` 通知共享撤销状态；由会话线程执行 Detach。后端取图、等待复用帧资源时分段检查该状态；撤销若发生在成功获取图像之后、提交之前，必须走该后端的图像退休路径。零尺寸和暂停不进入取图/提交。用 `SurfaceId`、设备实例代次和成功提交序号关联验收样本。

### 任务 3 · 固定 The Forge 并建立独立编译边界

**文件与接口。** 添加表中 gitlink 和 ThirdParty 目录；新建 `Source/ThirdParty/TheForge/{Windows,Linux,Android,Apple}.cmake`、`Tests/Toolchain/TheForge/{CMakeLists.txt,Compile.cpp}`，修改 `Tests/Toolchain/CMakeLists.txt`。普通目标 `ThirdParty::TheForge` 私有供 Renderer 使用。按平台将所需 C、C++、Objective-C/Objective-C++ 源编进同一静态依赖（SHARED 消费时为 PIC），每个图形进程仅 Renderer 一处持有该依赖。

- [ ] 编译/链接检查 `PanguForgeLinkCheck` 引用实际 `initRenderer/exitRenderer` 和对应后端；验证链接不含上游 Application main，普通消费源码仍保有 C++23 `char8_t` 语义。先执行 `cmake --build --preset windows --target PanguForgeLinkCheck` 观察缺失依赖的 RED。
- [ ] 固定源码、许可证和补丁清单，声明真实编译单元、系统库、必需 SDK/运行库与 GPU 配置资源。依照上游平台工程及固定头文件核对源清单。兼容补丁放在 `Patch/`，配置时核验原始输入并在构建目录应用，禁止修改 submodule 工作树。Apple 在所需作用域启用 OBJC/OBJCXX。只在图形目标或显式图形测试需要时接入该树，复用 `pangu_add_owned_subdirectory`。
- [ ] 在 Windows 核对编译数据库采用 preset 的 `clang.exe/clang++.exe`，局部处理 MSVC 1929 白名单、D3D12 SDK/GUID 及 C 源诊断。依次用 `windows/linux/macos/android-arm64/ios-arm64` preset 构建该目标；检查非系统导入、目标架构和最终链接。重跑任务 1 的无图形用例。提交 `build: 固定 The Forge v1.63 并接入五平台工具链`。

### 任务 4 · Windows 端到端首帧与真正的常驻 Renderer

**文件与接口。** 创建 RendererModule、RendererAccess、`Private/Backends/{ForgeBackend.cpp,D3D12.cpp}`、图形 Application 接线与 Win32.cpp，扩展任务 1 函数以支持 `KIND GRAPHICS`。`QueryRendererModule() noexcept -> const pg::module::ModuleDefinition*`，`AcquireRenderer(const EngineHost&) noexcept -> Result<module::ScopeAccess<RendererSession>>`。仿照 `EcsModule.cpp`、`WorldAccess.cpp` 注册 Host Scope Concrete，固定新 ModuleId 与 DeclarationId（各生成一次，提交前查重），使用 `pangu_configure_module`、`pangu_export_module_query`。新增 `Tests/Renderer/{HostTests.cpp,BackendTests.cpp,Project/FirstFrame.cpp,Acceptance/RunDesktop.cmake}`。

- [ ] 增加 `Host.RendererOwnedByExactHost`、`Backend.D3D12RemovedDuringPresentOrWait` 与 `Renderer.DesktopFrame`。前者断言借用属于指定宿主、失败打开后可重试、先关闭会话再释放借用；后者在设备移除时断言 `upstreamResetRequests == 0` 且报告原生错误。桌面测试要求真实 Project 与明确定义的 Project 回调链接。
- [ ] `check(PanguRendererTests, 'PanguRendererTests.(Host|Backend.D3D12)')`；运行桌面用例，确认缺失生产模块或原生入口的 RED。
- [ ] 实现 `PanguRenderer`、`Pangu::Renderer` 与私有 ForgeBackend；应用启动时在 `AdditionalResidentQueries` 加入 Query，Ready 后获取借用。Project 只包含 `PanguApplication.h`，实现回调 `Result<LoopControl> pg::app::OnFrame(GraphicsContext&) noexcept`。`LoopControl` 取 `kContinue/kStop`；`GraphicsContext::Present(LinearColor) noexcept -> Result<FrameResult>`、`ReopenGraphics() noexcept -> Status`。宿主接收业务首错并负责安全停止；设备失效时由业务显式选择关闭或重开。图形消费夹具目标名为 `FirstFrame`。轻量颜色与结果类型的定义须可由 Project 通过 Application 头使用，头文件依赖不向 Project 传播 Forge SDK。其余类型在 Engine 公共头中定义或转发，禁止 Project 直接登记 Module。
- [ ] 实现 Win32 事件循环、自建 HWND、客户区尺寸和 D3D12 Agility 导出/部署。失焦继续更新；最小化或零尺寸暂停。捕获 Present、Fence 等待及设备移除错误，关闭路径覆盖最后一帧错误。无图形入口与图形入口分别编入最终目标，避免 Renderer 导入污染工具。Module 的 Factory 创建冷会话，开设备留到显式 Open。
- [ ] 重跑 Host/Backend 用例。构建真实消费夹具，在 Windows GPU 上执行 `Renderer.DesktopFrame`，验证 120 次接纳、A/B 两色、resize、正常关闭；两种模式的隔离部署在任务 9 复核。提交 `feat: 通过 Engine 图形入口交付 D3D12 首帧`。

### 任务 5 · Vulkan 安全取图、提交、呈现与 Linux 首帧

**文件与接口。** 实现 `Private/Backends/Vulkan.cpp`、Xlib.cpp，新增 `Source/ThirdParty/TheForge/Patch/VulkanWsi.patch`，扩展 BackendTests 和 RunDesktop.cmake。沿用任务 2 私有接口，新增的原生结果桥仅在 Renderer 与固定版本的 Vulkan 编译单元间可见，保留 `VkResult` 和失败阶段。

- [ ] 增加 `Backend.VulkanAcquireCancel`、`VulkanSubmitAfterReset`、`VulkanRetireOldPresentQueue`、`VulkanLossOnDrain`。由受控原生调用返回 TIMEOUT、OUT_OF_DATE、SURFACE_LOST、DEVICE_LOST；固定断言：提交失败后呈现次数为零；提交成功但呈现失败时保留提交记录；延迟呈现完成时即刻 resize/stop 两条路径均不能提前释放旧同步对象。运行 `check(PanguRendererTests, 'Backend.Vulkan')` 观察 RED（在 Linux 以 linux preset 执行）。
- [ ] 在取图、提交、呈现、Fence/队列等待处保留原生结果；取图采用有限等待，超时先处理撤销再重试；空设备候选返回阶段错误。按交换链图像管理 present 等待信号量。活跃链重用依据重新获取同一图像及其完成信号；退休旧链单独建立呈现完成依据。优先使用已启用的 swapchain maintenance present fence；可选路径用已启用的 present ID/wait 跟踪成功排队的呈现。按原生返回码区分是否已排队、是否仍须等待，覆盖最后一条链与独立 present queue；针对无相应扩展的目标核验可证明的退休路径。有能力缺口的设备记录打开失败及原因，保留对应平台的验收缺项。
- [ ] 实现 Xlib 窗口与事件循环，转交有效 Display/Window 与像素尺寸，驱动同一图形回调。测试非阻塞取图时撤销 A、挂接 B 后接收迟到 A 通知；在 X11 客户区采集两种已知颜色。
- [ ] Linux 运行 Backend 用例和 `Renderer.DesktopFrame`，在物理 Vulkan 设备验证退出；WSLg/llvmpipe 附记为软件回归。任务 7 复用同一 Vulkan 实现。提交 `feat: 实现 Vulkan 会话退休与 X11 呈现`。

### 任务 6 · Metal 异步结果与 macOS 首帧

**文件与接口。** 实现 `Private/Backends/Metal.mm`、MacApplication.mm，新建 `Source/ThirdParty/TheForge/Patch/MetalCompletion.patch`。与任务 5 并列消费任务 2、3、4 的接口；任务 8 复用同一后端。

- [ ] 在 BackendTests 中验证 `Backend.MetalNilDrawable` 和 `MetalLastCompletionBeforeClose`。返回 nil 时 `accepted == 0 && deviceOpens == 1`；关停期间注入最后一次命令缓冲错误，断言 Close 带回该阶段错误、每个相关回调只退休一次。用 macos preset 执行 `check(PanguRendererTests, 'Backend.Metal')` 观察 RED。
- [ ] 取 drawable 后检查对象本身，暂时不可用返回 kSkipped；跟踪渲染和呈现相关命令缓冲的完成，回调只发布拥有型结果，状态由会话线程收取。关闭和单帧路径均消化异步错误；回调收齐后才释放相关状态。AppKit 入口持有 NSView/CAMetalLayer 并协调设备与 drawableSize，以实际像素挂接。
- [ ] 重跑测试，并在具备 Metal 的 macOS 运行 `Renderer.DesktopFrame`；对无 Metal 的环境另验初始化失败可清理。提交 `feat: 实现 Metal 完成回调与 macOS 呈现`。

### 任务 7 · Android APK、窗口撤销与 Activity 正常退出

**文件与接口。** 新建 AndroidApplication.cpp、`Source/Application/Platform/Android/{AndroidManifest.xml.in,PanguActivity.java.in,build.gradle.kts.in}` 和 `Tests/Renderer/Acceptance/RunAndroid.ps1`。复用 Vulkan 后端。`pangu_add_application` 为 Android 图形目标生成 NativeActivity SHARED 入口及 Gradle 打包输入；应用标识由消费 Project 给出。JNI/glue、原生库名和 manifest 元数据保持一致。

- [ ] 新建测试入口 `RunAndroid.ps1 -Apk <path> -Serial <device> -Mode <mode>`；先在未具备本任务生命周期处理的 APK 上观察测试失败。断言每次依序出现 `accepted >= 120`、Renderer 释放、native 线程返回、Activity.onDestroy 完成，且本次进程无 FORTIFY/SIGABRT、Java 未捕获异常或 ANR。验签及核对已装 versionCode/包摘要后才开始。
- [ ] 用 NDK r30 兼容的事件轮询；窗口终止通知与会话线程同步完成脱离，处理后台、恢复及新窗口。每实例一次 finish 请求，销毁后从 android_main 返回；仅注销注册成功的监听，用原注册管理器注销并收尾 JNI 回调。不依赖上游 IApp 或其 exit(0)。按 ABI 装入原生依赖、preset 所需的 C++ 运行库、gpu.cfg/gpu.data；Modular 通过实际导入与设备加载记录确认独立 Renderer.so。
- [ ] Windows 通过 android-arm64 构建并安装签名 APK。小米禁用 USB 安装时可经下载目录和系统安装器更新，再执行同一脚本。至少一模式覆盖同进程重新打开、后台暂停提交及恢复后的第二颜色。重跑上述脚本 GREEN；提交 `feat: 交付 Android 图形入口与安全窗口销毁`。

### 任务 8 · iOS Monolithic 应用及可恢复的前后台呈现

**文件与接口。** 新建 IosApplication.mm、`Source/Application/Platform/iOS/Info.plist.in`、`Tests/Renderer/Acceptance/RunIos.sh`，复用 Metal 后端。UIKit 主线程管理带 CAMetalLayer 的 UIView；应用入口转交帧回调，Project 源码与桌面相同。仅图形应用交付分支限定 iOS Monolithic，保留既有 ECS 等模块的交叉链接测试。

- [ ] 编写 `RunIos.sh <signed-app> <device-id>`，依 bundle ID 安装启动并获取这次会话的测量结果。要求 `accepted >= 120`、有 A/B 像素证据、后台确认暂停后新提交增量为零、恢复后设备标识保持且有新帧、显式停止时会话清理完成。先验证未包含入口或实测证据的产物会失败。
- [ ] 完成 arm64 App bundle、GPU 配置资源、可配置开发签名与 provisioning；两种 Apple 目标分别选用对应 SDK。前台开始/恢复调度，后台暂停，detach/stop 在主线程与 Metal 完成路径间安全收尾。显式停止的完成凭证来自 Application 生命周期，不能依赖进程退出回调。两端只在 Apple 平台启用相应编译语言，iOS 输入签名身份、profile 和设备标识由执行环境提供。
- [ ] 经共享源码根执行 `bash Scripts/build.sh configure --platform ios --link-mode Monolithic`，构建独立消费应用并检查 Mach-O、Framework 依赖和签名；在具备设备部署能力的宿主上运行设备脚本。只有构建或签名的情况在记录中标成部分验证。提交 `feat: 交付 iOS 单体首帧与生命周期接线`。

### 任务 9 · 隔离部署、九个实际运行组合与归档

**文件与接口。** 在 `Tests/Renderer/Acceptance/{BuildProject.cmake,VerifyDeployment.cmake,RunDesktop.cmake,RunAndroid.ps1,RunIos.sh}` 收束夹具与产物核验。`BuildProject.cmake` 接受 `PANGU_SOURCE_ROOT`、`PANGU_TARGET_PRESET`、`PANGU_LINK_MODE`、`PANGU_ACCEPTANCE_ROOT`，在指定宿主本地 BuildRoot 下构建同一 Project，输出 `stage/<mode>/`。沿用 ProjectIntegration 的真实 Engine revision 检查，正式运行固定干净提交。更新文件表中的 README、wiki 和本计划。

- [ ] 先让验收器处理三个负对照：移走隔离部署副本中的必需 GPU 配置、Modular 副本遗漏 Renderer、一直返回第一帧颜色。三者都应失败；系统库和驱动仍允许使用。四个支持 Modular 的平台分别运行同一 Project 的无图形工具，只部署它的闭包并实际完成 Context 操作。Android 的无图形夹具产出 NDK 可执行文件，在真机独立测试目录内启动，搜索路径仅包含该目录的自有库和系统库。
- [ ] 实现部署及记录。Windows 核对 PE 导入、真实加载路径及 Agility 版本；Linux/macOS 核对 ELF/Mach-O 依赖和 RPATH，并运行来自部署目录的应用；Android 检查 APK 内容、ABI 及应用实际加载映射；iOS 检查签名、bundle 资源与设备运行。用独立运行环境或安装包隔离源码和构建树，验证自有运行库与 GPU 配置的实际读取来源。仅更换工作目录不足以覆盖绝对 RPATH。
- [ ] 在各自宿主通过 `cmake -DPANGU_SOURCE_ROOT=<共享源码根> -DPANGU_TARGET_PRESET=<preset> -DPANGU_LINK_MODE=<mode> -DPANGU_ACCEPTANCE_ROOT=<宿主本地BuildRoot>/Tests/Renderer -P Tests/Renderer/Acceptance/BuildProject.cmake` 构建并部署。桌面执行 `ctest --preset <桌面平台> --no-tests=error -R '^Renderer\.DesktopFrame$' --output-on-failure`，移动执行任务 7、8 的宿主设备脚本。每个组合记录实际命令、提交、GPU/驱动、模式、加载位置、计数、采样及关闭结果。
- [ ] 统一按线性 RGBA 先清 A `(0.05,0.32,0.58,1)`，再清 B `(1,0,0,1)`。原生接纳累计至少 120 次，跳帧单列，提交/呈现/异步执行错误使正常用例失败；两阶段各取样并按颜色空间与通道顺序比较（8 位 sRGB 的 A 为 `#3F99C8`，B 为 `#FF0000`）。生命周期用例在恢复或 resize 后切 B；每平台至少一模式验证失焦仍刷新或前后台切换、暂停区间新提交为零、健康设备身份不变、恢复后采到 B。
- [ ] 运行所改 C++ 的仓库 clang-format 和基于实际编译数据库的 clang-tidy、CMake 格式与定向回归；核对无图形既有消费以及 ECS 移动链接入口。按 [Tests 维护规范](../../../Tests/README.md) 归档计数含义、工具链、警告、失败和复测；硬件、窗口系统、开发签名或部署工具暂缺时记录缺项，不将交叉链接计为设备通过。确认必要结论已归档、无任务依赖后只清理本计划的 `.superpowers/sdd/renderer-first-frame/`。提交 `test: 验收 Renderer 九组部署与首帧生命周期`。

## 收尾记录

本次停止源于 Renderer 改用原版 Forge 的公开 `IGraphics`。已交付的 Application 接线、Host Scope 借用和独立 Project 夹具可作为迁移基础。功能补丁和旧后端相关测试由迁移计划任务 2 处理。新路径的首帧、部署与平台验证由任务 3 重新取证。

| 原任务 | 停止时状态 | 关键提交及遗留项 |
| --- | --- | --- | --- |
| 1 · 无图形 Project | 已实施并通过逐项复评 | `14c1675`、`3c6eacd`。Windows 两种链接模式均完成 Context 创建、关闭和搬迁运行。 |
| 2 · 会话与资源退休 | 已实施并通过逐项复评 | `d0e1e01`、`6d6d032`。Windows Modular 19 个定向用例各通过三次。 |
| 3 · 固定 Forge 与工具链 | 已实施并通过逐项复评 | `3219e8b`、`9386a4e`。五套 preset 链接检查通过，另验证 macOS arm64 链接。 |
| 4 · Windows D3D12 | 已实施并通过逐项复评 | `971de3e` 至 `dd437d5`。Modular 实机首帧通过，完整生命周期与双模式部署仍待验。 |
| 5 · Linux Vulkan | 已实施并通过逐项复评 | `77d635c` 至 `af2091c`。软件首帧通过，物理 Vulkan GPU 验收缺失。 |
| 6 · macOS Metal | 部分实施与验证 | `18541d2`。异步错误定向测试和无设备退出已验证，首次评审的 Metal 上下文 ARC 引用修正与物理 GPU 首帧均未收尾。 |
| 7–8 · Android 与 iOS 入口 | 生产入口未实施 | 已有链接验证和历史探针。两平台的正式入口、打包和真机验收仍待安排。 |
| 9 · 九组产品验收 | 整体验收未完成 | 已取得个别组合的运行证据。独立环境、部署负对照和五平台完整生命周期尚无整体验收结论。 |

### 已取得的原路径验收证据

| 平台 | 旧路径验证 | 留缺 |
| --- | --- | --- |
| Windows / `windows` | `22bffe4`，Modular 真实 Radeon，resize 前后为 `#3F99C8` 和 `#FF0000`，122 次接纳、9 次跳过、0 次失败，退出 0。 | 图形 Monolithic、最小化与失焦及完整部署矩阵。 |
| Linux / `linux` | `af2091c`，WSLg llvmpipe 软件回归，resize 前后采到两种目标色，140 次接纳，退出 0。 | 物理 Vulkan GPU、完整生命周期及两种模式的正式部署。 |
| macOS / `macos` | `18541d2`，独立 Project 观察到 `kOpen/kNoDevice` 与日志关闭，CTest 将物理呈现标为跳过。 | 物理 Metal 首帧、像素采样、resize 及双模式部署。 |
| Android arm64 / `android-arm64` | `af2091c`，Forge 链接检查通过。 | 产品 APK、设备运行、窗口生命周期及双模式记录。 |
| iOS arm64 / `ios-arm64` | `18541d2`，Renderer 消费探针通过 Monolithic 链接。 | 生产入口、签名安装及真机生命周期。 |

本节归档旧方案的关键提交、验收证据与遗留项。2026-11-24 承接方案按约定范围通过验收，旧方案的本地执行目录已清理。[承接方案结项记录](2026-11-20-renderer-forge-igraphics.md#迁移结项)
