#include "ForgeBackend.h"

#include "../FrameBackend.h"
#include "../NativeSurface.h"

#include <Error.h>
#include <RendererTypes.h>
#include <Result.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#ifndef __ANDROID__
#include <array>
#endif
#ifdef _WIN32
#include <iterator>
#endif

#ifdef _WIN32
#include <algorithm>
#elifdef __APPLE__
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
#include <TargetConditionals.h>
#include <cstring>
#if TARGET_OS_IPHONE
#import <UIKit/UIView.h>
#else
#import <AppKit/NSView.h>
#endif
#elifndef __ANDROID__
#include <filesystem>
#include <system_error>
#endif

#include <Common_3/Graphics/GraphicsConfig.h>
#include <Common_3/Graphics/Interfaces/IGraphics.h>
#include <Common_3/OS/Interfaces/IOperatingSystem.h>
#include <Common_3/Resources/ResourceLoader/ThirdParty/OpenSource/tinyimageformat/tinyimageformat_base.h>
#include <Common_3/Utilities/Interfaces/IFileSystem.h>
#include <Common_3/Utilities/Interfaces/ILog.h> // IWYU pragma: keep

#if defined(__linux__) && !defined(__ANDROID__)
#include "SurfacePresentQueue.h"
#include <Common_3/Graphics/ThirdParty/OpenSource/VulkanSDK/include/vulkan/vulkan_core.h>
#include <Common_3/Graphics/ThirdParty/OpenSource/VulkanSDK/include/vulkan/vulkan_wayland.h>
#endif

#ifdef _WIN32
// Forge 图形头已按目标架构载入 windows.h。
#include <libloaderapi.h>
#include <minwindef.h>
#include <stringapiset.h>
#include <winnls.h>
#include <winuser.h>
#endif
// Forge 的分配宏要求此头在标准库头之后引入。
#include <Common_3/Utilities/Interfaces/IMemory.h>

#ifdef __ANDROID__
extern "C" WindowDesc gWindow;
#endif

#ifdef Status
#undef Status
#endif

namespace pg::renderer::detail
{
namespace
{
constexpr auto* kAppName = "PanguRenderer";

[[nodiscard]] std::unexpected<Error> Failure(RendererStage stage,
                                             RendererError cause = RendererError::kNativeFailure) noexcept
{
    return std::unexpected(MakeRendererError(stage, cause));
}

constexpr auto* kGpuDataFile = "gpu.data";
constexpr auto* kGpuConfigFile = "gpu.cfg";

#ifndef __ANDROID__
constexpr auto kGpuDataFileBytes = std::string_view{kGpuDataFile}.size() + 1;
static_assert(kGpuDataFileBytes > std::string_view{kGpuConfigFile}.size());

[[nodiscard]] constexpr bool ForgeResourcePathFits(std::size_t prefixBytes) noexcept
{
    // Forge 会把资源目录、文件名和结尾 NUL 放入同一个固定缓冲区。
    return prefixBytes > 0 && prefixBytes <= FS_MAX_PATH - kGpuDataFileBytes;
}

[[nodiscard]] bool ForgeResourceDirectory(std::array<char, FS_MAX_PATH>& path) noexcept
{
#ifdef _WIN32
    std::array<wchar_t, FS_MAX_PATH> image{};
    const auto length = GetModuleFileNameW(nullptr, image.data(), static_cast<DWORD>(image.size()));
    if (length == 0 || length == image.size())
    {
        return false;
    }
    const auto slash = std::find(std::make_reverse_iterator(image.begin() + length), image.rend(), L'\\');
    if (slash == image.rend())
    {
        return false;
    }
    *slash.base() = L'\0';
    const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, image.data(), -1, path.data(),
                                           static_cast<int>(path.size()), nullptr, nullptr);
    return bytes > 0 && ForgeResourcePathFits(static_cast<std::size_t>(bytes - 1));
#elifdef __APPLE__
#if TARGET_OS_IPHONE
    NSURL* const directory = NSBundle.mainBundle.resourceURL;
#else
    NSURL* const directory = NSBundle.mainBundle.executableURL.URLByDeletingLastPathComponent;
#endif
    NSString* const directoryPath = [directory.path stringByAppendingString:@"/"];
    return [directoryPath getFileSystemRepresentation:path.data() maxLength:path.size()] == YES &&
           ForgeResourcePathFits(std::strlen(path.data()));
#else
    std::error_code error;
    auto directory = std::filesystem::read_symlink("/proc/self/exe", error).parent_path();
    if (error || directory.empty())
    {
        return false;
    }
    // Forge 的 Unix IO 直接连接资源目录和文件名。
    directory /= "";
    const auto& bytes = directory.native();
    if (!ForgeResourcePathFits(bytes.size()))
    {
        return false;
    }
    bytes.copy(path.data(), bytes.size());
    return true;
#endif
}
#endif

[[nodiscard]] WindowHandle ForgeWindow(const NativeSurface& surface, PixelSize size) noexcept
{
    WindowHandle window{};
#ifndef __APPLE__
    (void)size;
#endif
#ifdef _WIN32
    if (const auto* handle = std::get_if<Win32Surface>(&surface.Handle());
        handle != nullptr && IsWindow(handle->Window) != FALSE)
    {
        window.type = WINDOW_HANDLE_TYPE_WIN32;
        window.window = handle->Window;
    }
#elifdef __ANDROID__
    if (const auto* handle = std::get_if<AndroidSurface>(&surface.Handle()); handle != nullptr)
    {
        window.type = WINDOW_HANDLE_TYPE_ANDROID;
        window.window = handle->Window;
    }
#elifdef __APPLE__
    if (const auto* handle = std::get_if<MetalSurface>(&surface.Handle()); handle != nullptr)
    {
#if TARGET_OS_IPHONE
        UIView* const view = (__bridge UIView*)handle->View;
#else
        NSView* const view = (__bridge NSView*)handle->View;
#endif
        CAMetalLayer* const layer = (__bridge CAMetalLayer*)handle->Layer;
        if (view != nil && layer != nil && view.layer == layer)
        {
            layer.drawableSize = CGSizeMake(size.Width, size.Height);
            window.window = handle->View;
        }
    }
#else
    if (const auto* handle = std::get_if<WaylandSurface>(&surface.Handle());
        handle != nullptr && handle->Display != nullptr && handle->Surface != nullptr)
    {
        window.type = WINDOW_HANDLE_TYPE_WAYLAND;
        // Forge 的 C 接口用 type 选择匿名联合体的 Wayland 成员。
        // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
        window.wl_display = handle->Display;
        window.wl_surface = handle->Surface;
        // NOLINTEND(cppcoreguidelines-pro-type-union-access)
    }
#endif
    return window;
}

#if defined(__linux__) && !defined(__ANDROID__)
[[nodiscard]] bool CanPresentToWayland(VkInstance instance, VkPhysicalDevice gpu, const WaylandSurface& window) noexcept
{
    if (window.Display == nullptr || window.Surface == nullptr || vkCreateWaylandSurfaceKHR == nullptr ||
        vkDestroySurfaceKHR == nullptr || vkGetPhysicalDeviceSurfaceSupportKHR == nullptr)
    {
        return false;
    }
    VkWaylandSurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    info.display = window.Display;
    info.surface = window.Surface;
    VkSurfaceKHR surface{};
    if (vkCreateWaylandSurfaceKHR(instance, &info, nullptr, &surface) != VK_SUCCESS)
    {
        return false;
    }
    const bool supported = HasSurfacePresentQueue(gpu, surface, vkGetPhysicalDeviceQueueFamilyProperties,
                                                  vkGetPhysicalDeviceSurfaceSupportKHR);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    return supported;
}

[[nodiscard]] std::optional<std::uint32_t> SelectWaylandGpu(RendererContext& context, const NativeSurface* surface)
{
    // gpu.cfg 的排序可优先选择随后会被厂商规则拒绝的集显。只对可用设备排序，
    // 并将选中项映射回 Forge Context，保留驱动筛选和用户的 GPU 偏好。
    const auto devices = std::span{context.mGpus}.first(context.mGpuCount);
    const auto* window = surface != nullptr ? std::get_if<WaylandSurface>(&surface->Handle()) : nullptr;
    std::vector<GpuDesc> supported;
    std::vector<std::uint32_t> indices;
    for (auto [index, device] : std::views::enumerate(devices))
    {
        if (device.mGraphicsQueueSupported && device.mGpuVendorPreset.mPresetLevel >= GPU_PRESET_VERYLOW &&
            util_check_is_gpu_supported(&device) && checkDriverRejectionSettings(&device) &&
            (surface == nullptr ||
             (window != nullptr && CanPresentToWayland(context.mVk.pInstance, device.mVk.pGpu, *window))))
        {
            supported.push_back(device);
            indices.push_back(static_cast<std::uint32_t>(index));
        }
    }
    if (supported.empty())
    {
        return std::nullopt;
    }
    const auto best = util_select_best_gpu(supported.data(), static_cast<std::uint32_t>(supported.size()));
    return best < indices.size() ? std::optional{indices.at(best)} : std::nullopt;
}
#endif

class ForgeBackend final : public FrameBackend
{
public:
    [[nodiscard]] Status Open(RendererOpenDesc desc) noexcept override;
    [[nodiscard]] Status Attach(NativeSurface surface, PixelSize size) noexcept override;
    [[nodiscard]] Result<AcquireResult> Acquire() noexcept override;
    [[nodiscard]] Status Submit(LinearColor color) noexcept override;
    void PresentImage() noexcept override;
    void RetireSurface() noexcept override;
    void Close() noexcept override;

private:
    enum class RuntimeState : std::uint8_t
    {
        kClosed,
        kMemory,
        kFileSystem,
        kLog,
        kGpuConfig,
    };

    [[nodiscard]] Status OpenRuntime(RendererOpenDesc desc) noexcept;
    [[nodiscard]] Status OpenCommands() noexcept;

    RuntimeState Runtime = RuntimeState::kClosed;
#if defined(DIRECT3D12) || defined(METAL) || (defined(VULKAN) && !defined(__ANDROID__))
    RendererContext* Context{};
#endif
    Renderer* Device{};
    Queue* GraphicsQueue{};
    CmdPool* CommandPool{};
    Cmd* Command{};
    Fence* CommandFence{};
    Semaphore* ImageAvailable{};
    SwapChain* Chain{};
    std::vector<Semaphore*> ReadyToPresent;
    std::uint32_t Image{};
};

Status ForgeBackend::OpenRuntime(RendererOpenDesc desc) noexcept
{
#ifdef __ANDROID__
    if (desc.Activity == nullptr)
    {
        return Failure(RendererStage::kOpen, RendererError::kInvalidState);
    }
#endif
    // 工具模式不挂载 RD_LOG，沿用 Forge 工具的无文件内存诊断。
    if (!initMemAlloc(nullptr))
    {
        return Failure(RendererStage::kOpen);
    }
    Runtime = RuntimeState::kMemory;

    FileSystemInitDesc files{.pAppName = kAppName, .pPlatformData = desc.Activity, .mIsTool = desc.Activity == nullptr};
    if (!initFileSystem(&files))
    {
        return Failure(RendererStage::kOpen);
    }
    Runtime = RuntimeState::kFileSystem;

    // Forge 的头经外部配置加载，include-cleaner 无法归属日志声明。
    initLog(nullptr, DEFAULT_LOG_LEVEL); // NOLINT(misc-include-cleaner)
    Runtime = RuntimeState::kLog;

#ifndef __ANDROID__
    std::array<char, FS_MAX_PATH> directory{};
    if (!ForgeResourceDirectory(directory))
    {
        return Failure(RendererStage::kOpen);
    }
    fsSetPathForResourceDir(pSystemFileIO, RD_OTHER_FILES, directory.data());
    fsSetPathForResourceDir(pSystemFileIO, RD_GPU_CONFIG, directory.data());
#endif

    FileStream stream{.pIO = nullptr, .mMode = FM_READ, .mUser = {}};
    if (!fsOpenStreamFromPath(RD_OTHER_FILES, kGpuDataFile, FM_READ, &stream))
    {
        return Failure(RendererStage::kOpen);
    }
    fsCloseStream(&stream);
    if (!fsOpenStreamFromPath(RD_GPU_CONFIG, kGpuConfigFile, FM_READ, &stream))
    {
        return Failure(RendererStage::kOpen);
    }
    fsCloseStream(&stream);
    initGPUConfiguration(nullptr);
    Runtime = RuntimeState::kGpuConfig;
#ifdef __ANDROID__
    gWindow.handle.activity = desc.Activity;
#endif
    return {};
}

Status ForgeBackend::Open(RendererOpenDesc desc) noexcept
{
    if (const auto started = OpenRuntime(desc); !started)
    {
        return started;
    }

#if defined(DIRECT3D12) || defined(METAL) || (defined(VULKAN) && !defined(__ANDROID__))
    // v1.63 的 initRenderer 假设具备可用 Context，先通过公开接口检查。
    const RendererContextDesc context{}; // NOLINT(bugprone-invalid-enum-default-initialization)
    initRendererContext(kAppName, &context, &Context);
    if (Context == nullptr || Context->mGpuCount == 0)
    {
        return Failure(RendererStage::kOpen, RendererError::kNoDevice);
    }
    RendererDesc settings{}; // NOLINT(bugprone-invalid-enum-default-initialization)
    settings.pContext = Context;
#ifdef DIRECT3D12
    // 显式 Context 使 Forge 按索引选择设备，仍采用 Forge 的 GPU 配置和驱动筛选。
    settings.mGpuIndex = util_select_best_gpu(std::data(Context->mGpus), Context->mGpuCount);
    if (settings.mGpuIndex >= Context->mGpuCount)
    {
        return Failure(RendererStage::kOpen, RendererError::kNoDevice);
    }
    auto& candidate = Context->mGpus[settings.mGpuIndex]; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    if (!util_check_is_gpu_supported(&candidate) || !checkDriverRejectionSettings(&candidate))
    {
        return Failure(RendererStage::kOpen, RendererError::kNoDevice);
    }
    if (candidate.mGpuVendorPreset.mPresetLevel < GPU_PRESET_VERYLOW)
    {
        return Failure(RendererStage::kOpen, RendererError::kNoDevice);
    }
#elifdef VULKAN
    const auto selected = SelectWaylandGpu(*Context, desc.PresentationSurface);
    if (!selected)
    {
        return Failure(RendererStage::kOpen, RendererError::kNoDevice);
    }
    settings.mGpuIndex = *selected;
#endif
#else
    // 与 Forge 样例一致，零初始化的描述符采用后端默认设置。
    const RendererDesc settings{}; // NOLINT(bugprone-invalid-enum-default-initialization)
#endif
    initRenderer(kAppName, &settings, &Device);
    if (Device == nullptr)
    {
        return Failure(RendererStage::kOpen, RendererError::kNoDevice);
    }
#if defined(DIRECT3D12) && defined(_WINDOWS) && defined(ENABLE_GRAPHICS_VALIDATION)
    // 显式 Context 跳过 Forge 对多 GPU 的 Present 调试层误报过滤设置。
    Device->mDx.mSuppressMismatchingCommandListDuringPresent =
        Device->mDx.pDebugValidation != nullptr && Context->mGpuCount >= 2;
#endif
    setupGPUConfigurationPlatformParameters(Device, settings.pExtendedSettings);
    return OpenCommands();
}

Status ForgeBackend::OpenCommands() noexcept
{
    QueueDesc queue{};
    queue.mType = QUEUE_TYPE_GRAPHICS;
    initQueue(Device, &queue, &GraphicsQueue);
    if (GraphicsQueue == nullptr)
    {
        return Failure(RendererStage::kOpen);
    }
#ifdef METAL
    if (GraphicsQueue->pCommandQueue == nil)
    {
        return Failure(RendererStage::kOpen);
    }
#endif

    CmdPoolDesc pool{};
    pool.pQueue = GraphicsQueue;
    initCmdPool(Device, &pool, &CommandPool);
    if (CommandPool == nullptr)
    {
        return Failure(RendererStage::kOpen);
    }

    CmdDesc command{};
    command.pPool = CommandPool;
    initCmd(Device, &command, &Command);
    initFence(Device, &CommandFence);
    return Command != nullptr && CommandFence != nullptr ? Status{} : Failure(RendererStage::kOpen);
}

Status ForgeBackend::Attach(NativeSurface surface, PixelSize size) noexcept
{
    const auto window = ForgeWindow(surface, size);
#if defined(__linux__) && !defined(__ANDROID__)
    if (window.type != WINDOW_HANDLE_TYPE_WAYLAND)
#else
    if (window.window == decltype(window.window){})
#endif
    {
        return Failure(RendererStage::kAttach, RendererError::kInvalidSurface);
    }

#if defined(__linux__) && !defined(__ANDROID__)
    // Attach 可以接收与 Open 不同的窗口；进入 Forge 的交换链路径前检查实际 Surface。
    if (!CanPresentToWayland(Context->mVk.pInstance, Device->pGpu->mVk.pGpu,
                             std::get<WaylandSurface>(surface.Handle())))
    {
        return Failure(RendererStage::kAttach, RendererError::kPresentationUnavailable);
    }
#endif

    SwapChainDesc desc{};
    desc.mWindowHandle = window;
    desc.ppPresentQueues = &GraphicsQueue;
    desc.mPresentQueueCount = 1;
    desc.mImageCount = getRecommendedSwapchainImageCount(Device, &window);
    desc.mWidth = size.Width;
    desc.mHeight = size.Height;
    desc.mEnableVsync = true;
    desc.mColorSpace = COLOR_SPACE_SDR_SRGB;
    desc.mColorFormat = getSupportedSwapchainFormat(Device, &desc, desc.mColorSpace);
    if (desc.mImageCount == 0 || desc.mImageCount > std::numeric_limits<std::uint8_t>::max() ||
        desc.mColorFormat == TinyImageFormat_UNDEFINED)
    {
        return Failure(RendererStage::kAttach);
    }
    addSwapChain(Device, &desc, &Chain);
    if (Chain == nullptr || Chain->mImageCount == 0 || Chain->ppRenderTargets == nullptr)
    {
        return Failure(RendererStage::kAttach);
    }

    initSemaphore(Device, &ImageAvailable);
    if (ImageAvailable == nullptr)
    {
        return Failure(RendererStage::kAttach);
    }
    // 用后续取到同一图像作为其呈现信号量可复用的依据。
    ReadyToPresent.resize(Chain->mImageCount);
    for (auto& ready : ReadyToPresent)
    {
        initSemaphore(Device, &ready);
        if (ready == nullptr)
        {
            return Failure(RendererStage::kAttach);
        }
    }
    return {};
}

Result<AcquireResult> ForgeBackend::Acquire() noexcept
{
    // 单个命令池及取图信号量在上一帧 GPU 提交完成后才能复用。
    FenceStatus fenceStatus = FENCE_STATUS_INCOMPLETE;
    getFenceStatus(Device, CommandFence, &fenceStatus);
    if (fenceStatus == FENCE_STATUS_INCOMPLETE)
    {
        waitForFences(Device, 1, &CommandFence);
    }
    if (IsDetachRequested())
    {
        return AcquireResult::kReattach;
    }

    // Forge 用索引表达可观察的取图结果。
    Image = Chain->mImageCount;
    acquireNextImage(Device, Chain, ImageAvailable, nullptr, &Image);
#ifdef VULKAN
    if (Image == std::numeric_limits<std::uint32_t>::max())
    {
        return AcquireResult::kReattach;
    }
#endif
    if (Image >= Chain->mImageCount)
    {
        return Failure(RendererStage::kAcquire);
    }
#ifdef METAL
    // Metal 公布的索引仍可能关联一次未取得 drawable 的调用。
    if (Chain->mMTKDrawable == nil)
    {
        return AcquireResult::kSkipped;
    }
#endif
    return AcquireResult::kReady;
}

Status ForgeBackend::Submit(LinearColor color) noexcept
{
    // Acquire 已按交换链公布的图像数校验索引。
    auto* const target = Chain->ppRenderTargets[Image]; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (target == nullptr)
    {
        return Failure(RendererStage::kSubmit);
    }

    resetCmdPool(Device, CommandPool);
    beginCmd(Command);
    RenderTargetBarrier barrier{};
    barrier.pRenderTarget = target;
    barrier.mCurrentState = RESOURCE_STATE_PRESENT;
    barrier.mNewState = RESOURCE_STATE_RENDER_TARGET;
    cmdResourceBarrier(Command, 0, nullptr, 0, nullptr, 1, &barrier);

    BindRenderTargetsDesc bind{};
    bind.mRenderTargetCount = 1;
    auto& attachment = bind.mRenderTargets[0];
    attachment.pRenderTarget = target;
    attachment.mLoadAction = LOAD_ACTION_CLEAR;
    attachment.mStoreAction = STORE_ACTION_STORE;
    attachment.mClearValue = {.r = color.R, .g = color.G, .b = color.B, .a = color.A};
    attachment.mOverrideClearValue = 1;
    cmdBindRenderTargets(Command, &bind);
    cmdBindRenderTargets(Command, nullptr);

    std::swap(barrier.mCurrentState, barrier.mNewState);
    cmdResourceBarrier(Command, 0, nullptr, 0, nullptr, 1, &barrier);
    endCmd(Command);

    const QueueSubmitDesc submit{
        .ppCmds = &Command,
        .pSignalFence = CommandFence,
        .ppWaitSemaphores = &ImageAvailable,
        .ppSignalSemaphores = &ReadyToPresent.at(Image),
        .mCmdCount = 1,
        .mWaitSemaphoreCount = 1,
        .mSignalSemaphoreCount = 1,
        .mSubmitDone = false,
    };
    queueSubmit(GraphicsQueue, &submit);
    return {};
}

void ForgeBackend::PresentImage() noexcept
{
    const QueuePresentDesc present{
        .pSwapChain = Chain,
        .ppWaitSemaphores = &ReadyToPresent.at(Image),
        .mWaitSemaphoreCount = 1,
        .mIndex = static_cast<std::uint8_t>(Image),
        .mSubmitDone = true,
    };
    queuePresent(GraphicsQueue, &present);
}

void ForgeBackend::RetireSurface() noexcept
{
    if (Chain == nullptr)
    {
        return;
    }

    waitQueueIdle(GraphicsQueue);
#ifdef METAL
    // 上游 removeSwapChain 不清理这两个 ARC 强引用，由窗口层继续管理 View 与 Layer。
    Chain->mMTKDrawable = nil;
    Chain->pForgeView = nil;
#endif
    removeSwapChain(Device, std::exchange(Chain, nullptr));
    for (auto* ready : ReadyToPresent)
    {
        if (ready != nullptr)
        {
            exitSemaphore(Device, ready);
        }
    }
    ReadyToPresent.clear();
    if (ImageAvailable != nullptr)
    {
        exitSemaphore(Device, std::exchange(ImageAvailable, nullptr));
    }
}

void ForgeBackend::Close() noexcept
{
    if (CommandFence != nullptr)
    {
        exitFence(Device, std::exchange(CommandFence, nullptr));
    }
    if (Command != nullptr)
    {
        exitCmd(Device, std::exchange(Command, nullptr));
    }
    if (CommandPool != nullptr)
    {
        exitCmdPool(Device, std::exchange(CommandPool, nullptr));
    }
    if (GraphicsQueue != nullptr)
    {
        exitQueue(Device, std::exchange(GraphicsQueue, nullptr));
    }
    if (Device != nullptr)
    {
        exitRenderer(std::exchange(Device, nullptr));
    }
#if defined(DIRECT3D12) || defined(METAL) || (defined(VULKAN) && !defined(__ANDROID__))
    if (Context != nullptr)
    {
#ifdef METAL
        // Forge 直接释放 Context 的存储，ARC 不会析构设备和计数器对象。
        for (auto& gpu : Context->mGpus)
        {
            gpu.pCounterSetTimestamp = nil;
            gpu.pGPU = nil;
        }
#endif
        exitRendererContext(std::exchange(Context, nullptr));
    }
#endif

    if (Runtime >= RuntimeState::kGpuConfig)
    {
        exitGPUConfiguration();
    }
    if (Runtime >= RuntimeState::kLog)
    {
        exitLog(); // NOLINT(misc-include-cleaner)
    }
    if (Runtime >= RuntimeState::kFileSystem)
    {
        exitFileSystem();
    }
    if (Runtime >= RuntimeState::kMemory)
    {
        exitMemAlloc();
    }
#ifdef __ANDROID__
    gWindow.handle.activity = nullptr;
#endif
    Runtime = RuntimeState::kClosed;
}
} // namespace

std::unique_ptr<FrameBackend> MakeForgeBackend() noexcept { return std::make_unique<ForgeBackend>(); }
} // namespace pg::renderer::detail
