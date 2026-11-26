#include "ForgeBackend.h"

#include "../FrameBackend.h"
#include "../NativeSurface.h"

#include <Error.h>
#include <RendererTypes.h>
#include <Result.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <iterator>
#endif

#ifdef __APPLE__
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIView.h>
#else
#import <AppKit/NSView.h>
#endif
#elif !defined(__ANDROID__) && !defined(_WIN32)
#include <filesystem>
#include <optional>
#include <ranges>
#include <span>
#include <system_error>
#endif

#include <Common_3/Graphics/GraphicsConfig.h>
#include <Common_3/Graphics/Interfaces/IGraphics.h>
#include <Common_3/OS/Interfaces/IOperatingSystem.h>
#include <Common_3/Resources/ResourceLoader/Interfaces/IResourceLoader.h>
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

// Forge v1.63 资源加载器的外部入口，当前未在 IResourceLoader.h 声明。
const char* getShaderPlatformName(); // NOLINT(readability-identifier-naming)

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
constexpr auto* kVertexShaderFile = "Graph.vert";
constexpr auto* kFragmentShaderFile = "Graph.frag";
#ifdef DIRECT3D12
constexpr auto* kGraphicsRootFile = "default.rootsig";
#endif
#ifdef METAL
constexpr auto* kShaderSuffix = ".metal";
#else
constexpr auto* kShaderSuffix = "";
#endif

// 固定 Forge v1.63 的 FSL 头和变体记录。Vulkan 最多生成两套描述符索引变体。
struct FslHeader
{
    std::array<char, 4> Magic;
    std::uint32_t VariantCount;
    std::array<std::uint32_t, 7> Metadata;
};

struct FslVariant
{
    std::uint64_t Hash;
    std::uint64_t Offset;
    std::uint64_t Size;
};

static_assert(sizeof(FslHeader) == 36 && sizeof(FslVariant) == 24);

[[nodiscard]] bool ValidateShaderContainer(FileStream& stream, std::uint64_t requiredVariant) noexcept
{
    constexpr std::array kMagic{'@', 'F', 'S', 'L'};
    constexpr auto kMaximumVariantCount = 2U;
    const auto fileSize = fsGetStreamFileSize(&stream);
    FslHeader header{};
    if (std::cmp_less(fileSize, sizeof(header)) ||
        fsReadFromStream(&stream, &header, sizeof(header)) != sizeof(header) || header.Magic != kMagic ||
        header.VariantCount == 0 || header.VariantCount > kMaximumVariantCount)
    {
        return false;
    }

    const auto size = static_cast<std::uint64_t>(fileSize);
    std::uint64_t nextPayload = sizeof(header) + (sizeof(FslVariant) * header.VariantCount);
    bool found = header.VariantCount == 1;
    for (std::uint32_t index = 0; index < header.VariantCount; ++index)
    {
        FslVariant variant{};
        if (fsReadFromStream(&stream, &variant, sizeof(variant)) != sizeof(variant) || variant.Offset != nextPayload ||
            variant.Offset > size || variant.Size == 0 || variant.Size > size - variant.Offset ||
            variant.Size > std::numeric_limits<std::uint32_t>::max())
        {
            return false;
        }
        found |= variant.Hash == requiredVariant;
        nextPayload = variant.Offset + variant.Size;
    }
    return found && nextPayload == size;
}

[[nodiscard]] bool ShaderBinaryAvailable(const char* name, std::uint64_t requiredVariant) noexcept
{
    std::array<char, FS_MAX_PATH> path{};
    constexpr auto kMaxPathLength = FS_MAX_PATH - 1;
    const auto bytes =
        std::format_to_n(path.begin(), kMaxPathLength, "{}/{}{}", getShaderPlatformName(), name, kShaderSuffix).size;
    FileStream stream{.pIO = nullptr, .mMode = FM_READ, .mUser = {}};
    if (bytes > kMaxPathLength || !fsOpenStreamFromPath(RD_SHADER_BINARIES, path.data(), FM_READ, &stream))
    {
        return false;
    }
    const bool valid = ValidateShaderContainer(stream, requiredVariant);
    return fsCloseStream(&stream) && valid;
}

struct GpuTriangleVertex
{
    std::array<float, 2> Position;
    std::array<float, 4> Color;
};

static_assert(sizeof(GpuTriangleVertex) == sizeof(float) * 6);
constexpr std::uint32_t kTriangleVertexCount = 3;
constexpr std::uint32_t kTriangleVertexStride = sizeof(GpuTriangleVertex);

[[nodiscard]] GpuTriangleVertex PackVertex(const TriangleVertex& vertex) noexcept
{
    return {
        .Position = {vertex.X, vertex.Y},
        .Color = {vertex.Color.R, vertex.Color.G, vertex.Color.B, vertex.Color.A},
    };
}

#ifndef __ANDROID__
#ifdef DIRECT3D12
constexpr std::string_view kLongestShaderFile = kGraphicsRootFile;
#else
constexpr std::string_view kLongestShaderFile = kFragmentShaderFile;
#endif
constexpr auto kGpuDataFileBytes = std::string_view{kGpuDataFile}.size() + 1;
static_assert(kGpuDataFileBytes > std::string_view{kGpuConfigFile}.size());
static_assert(kLongestShaderFile.size() >= std::string_view{kVertexShaderFile}.size() &&
              kLongestShaderFile.size() >= std::string_view{kFragmentShaderFile}.size());

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
    [[nodiscard]] Status PrepareTriangle(bool indexed) noexcept override;
    [[nodiscard]] Result<AcquireResult> Acquire() noexcept override;
    [[nodiscard]] Status Submit(LinearColor color, FrameDraw draw) noexcept override;
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
    [[nodiscard]] Status OpenTriangleResources() noexcept;
    void OpenLoader() noexcept;
    [[nodiscard]] Status OpenIndexedMesh(const IndexedQuad& quad) noexcept;
    [[nodiscard]] Status OpenTriangleBuffer() noexcept;
    void RecordGeometry(const RenderTarget& target, FrameDraw draw) noexcept;
    void CloseTriangleResources() noexcept;

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
    Shader* TriangleShader{};
    Buffer* TriangleBuffer{};
    Buffer* QuadVertices{};
    Buffer* QuadIndices{};
    // 加载器拥有此信号量。首次图形提交等待上传写入可见。
    Semaphore* QuadUploadReady{};
    Pipeline* TrianglePipeline{};
    bool ShaderDirectoryAvailable{};
    bool RootSignatureReady{};
    bool ResourceLoaderReady{};
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
    constexpr std::string_view kShaderDirectory = "CompiledShaders/";
    const auto shaderOffset = std::string_view{directory.data()}.size();
    const auto shaderRelativeBytes = std::string_view{getShaderPlatformName()}.size() + 1 + kLongestShaderFile.size() +
                                     std::string_view{kShaderSuffix}.size() + 1;
    if (shaderOffset + kShaderDirectory.size() <= directory.size() - shaderRelativeBytes)
    {
        kShaderDirectory.copy(&directory.at(shaderOffset), kShaderDirectory.size());
        directory.at(shaderOffset + kShaderDirectory.size()) = '\0';
        fsSetPathForResourceDir(pSystemFileIO, RD_SHADER_BINARIES, directory.data());
        ShaderDirectoryAvailable = true;
    }
#else
    // Android 的打包应用由 Forge PathStatement 挂载 APK 内的资源目录。
    ShaderDirectoryAvailable = true;
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
    const auto commands = OpenCommands();
    return commands && desc.Quad != nullptr ? OpenIndexedMesh(*desc.Quad) : commands;
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

Status ForgeBackend::OpenTriangleResources() noexcept
{
    std::uint64_t variant = 0;
#ifdef VULKAN
    variant = Device->pGpu->mDescriptorIndexingExtension;
#endif
    if (!ShaderDirectoryAvailable || !ShaderBinaryAvailable(kVertexShaderFile, variant) ||
        !ShaderBinaryAvailable(kFragmentShaderFile, variant)
#ifdef DIRECT3D12
        || !ShaderBinaryAvailable(kGraphicsRootFile, variant)
#endif
    )
    {
        return Failure(RendererStage::kSubmit, RendererError::kShaderUnavailable);
    }

    RootSignatureDesc root{};
#ifdef DIRECT3D12
    root.pGraphicsFileName = kGraphicsRootFile;
#endif
    initRootSignature(Device, &root);
#ifdef DIRECT3D12
    RootSignatureReady = Device->mDx.pGraphicsRootSignature != nullptr;
#elifdef VULKAN
    RootSignatureReady = Device->mVk.pEmptyStaticSamplerDescriptorSet != nullptr;
#else
    RootSignatureReady = true;
#endif
    if (!RootSignatureReady)
    {
        return Failure(RendererStage::kSubmit, RendererError::kShaderUnavailable);
    }

    ShaderLoadDesc shader{};
    shader.mVert.pFileName = kVertexShaderFile;
    shader.mFrag.pFileName = kFragmentShaderFile;
    addShader(Device, &shader, &TriangleShader);
    if (TriangleShader == nullptr)
    {
        return Failure(RendererStage::kSubmit, RendererError::kShaderUnavailable);
    }

    return {};
}

void ForgeBackend::OpenLoader() noexcept
{
    if (ResourceLoaderReady)
    {
        return;
    }
    constexpr std::uint64_t kStagingBytes = 4096;
    ResourceLoaderDesc loader{.mBufferSize = kStagingBytes, .mBufferCount = 1, .mSingleThreaded = true};
    initResourceLoaderInterface(Device, &loader);
    ResourceLoaderReady = true;
}

[[nodiscard]] bool HasNativeBuffer(const Buffer* buffer) noexcept
{
    if (buffer == nullptr)
    {
        return false;
    }
#ifdef DIRECT3D12
    return buffer->mDx.pResource != nullptr;
#elifdef VULKAN
    return buffer->mVk.pBuffer != VK_NULL_HANDLE && buffer->mVk.pAllocation != nullptr;
#else
    return buffer->pBuffer != nil;
#endif
}

Status ForgeBackend::OpenIndexedMesh(const IndexedQuad& quad) noexcept
{
    OpenLoader();
    std::array<GpuTriangleVertex, 4> vertices{};
    std::ranges::transform(quad.Vertices, vertices.begin(), PackVertex);
    const bool unified = getUmaFlags() != UMA_SUPPORT_NONE;
    BufferLoadDesc vertex{};
    vertex.ppBuffer = &QuadVertices;
    vertex.mDesc.mSize = sizeof(vertices);
    vertex.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
    vertex.mDesc.mStartState = RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    vertex.mDesc.mMemoryUsage = unified ? RESOURCE_MEMORY_USAGE_CPU_TO_GPU : RESOURCE_MEMORY_USAGE_GPU_ONLY;
#ifdef VULKAN
    if (unified)
    {
        vertex.mDesc.mFlags = BUFFER_CREATION_FLAG_HOST_COHERENT;
    }
#endif
    vertex.mDesc.pName = "Indexed quad vertices";
    addResource(&vertex, nullptr);
    if (!HasNativeBuffer(QuadVertices))
    {
        return Failure(RendererStage::kOpen);
    }
    BufferLoadDesc index = vertex;
    index.ppBuffer = &QuadIndices;
    index.mDesc.mSize = sizeof(quad.Indices);
    index.mDesc.mDescriptors = DESCRIPTOR_TYPE_INDEX_BUFFER;
    index.mDesc.mStartState = RESOURCE_STATE_INDEX_BUFFER;
    index.mDesc.pName = "Indexed quad indices";
    addResource(&index, nullptr);
    if (!HasNativeBuffer(QuadIndices))
    {
        return Failure(RendererStage::kOpen);
    }

    // 两块资源就绪后再写入。Forge 的更新路径使用图形队列，避免独立传输队列族的所有权转移。
    const auto upload = [](Buffer* buffer, const auto& data, ResourceState state)
    {
        BufferUpdateDesc update{};
        update.pBuffer = buffer;
        update.mSize = sizeof(data);
        update.mCurrentState = state;
        beginUpdateResource(&update);
        if (update.pMappedData == nullptr)
        {
            return false;
        }
        std::memcpy(update.pMappedData, data.data(), sizeof(data));
        endUpdateResource(&update);
        return true;
    };
    const bool verticesWritten = upload(QuadVertices, vertices, RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    const bool indicesWritten = verticesWritten && upload(QuadIndices, quad.Indices, RESOURCE_STATE_INDEX_BUFFER);
    if (!unified && verticesWritten)
    {
        FlushResourceUpdateDesc flush{};
        flushResourceUpdates(&flush);
        waitForFences(Device, 1, &flush.pOutFence);
        QuadUploadReady = flush.pOutSubmittedSemaphore;
    }
    return indicesWritten ? Status{} : Failure(RendererStage::kOpen);
}

Status ForgeBackend::OpenTriangleBuffer() noexcept
{
    OpenLoader();
    BufferLoadDesc vertexBuffer{};
    vertexBuffer.ppBuffer = &TriangleBuffer;
    vertexBuffer.mDesc.mSize = sizeof(GpuTriangleVertex) * kTriangleVertexCount;
    vertexBuffer.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
    vertexBuffer.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
#ifdef VULKAN
    vertexBuffer.mDesc.mFlags |= BUFFER_CREATION_FLAG_HOST_COHERENT;
#endif
    vertexBuffer.mDesc.mStartState = RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    vertexBuffer.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
    vertexBuffer.mDesc.pName = "Triangle vertices";
    addResource(&vertexBuffer, nullptr);
    return TriangleBuffer != nullptr && TriangleBuffer->pCpuMappedAddress != nullptr ? Status{}
                                                                                     : Failure(RendererStage::kSubmit);
}

Status ForgeBackend::PrepareTriangle(bool indexed) noexcept
{
    if (!indexed && TriangleBuffer == nullptr)
    {
        if (const auto status = OpenTriangleBuffer(); !status)
        {
            return status;
        }
    }
    if (TrianglePipeline != nullptr)
    {
        return {};
    }
    // 交换链图像采用同一颜色格式与采样配置，创建 Pipeline 无须提前取图。
    const auto* target = *Chain->ppRenderTargets;
    if (target == nullptr)
    {
        return Failure(RendererStage::kSubmit);
    }
    if (TriangleShader == nullptr)
    {
        if (const auto opened = OpenTriangleResources(); !opened)
        {
            return opened;
        }
    }
    VertexLayout vertexLayout{};
    vertexLayout.mBindingCount = 1;
    vertexLayout.mBindings[0].mStride = kTriangleVertexStride;
    vertexLayout.mAttribCount = 2;
    auto& position = vertexLayout.mAttribs[0];
    position.mSemantic = SEMANTIC_POSITION;
    position.mFormat = TinyImageFormat_R32G32_SFLOAT;
    position.mOffset = offsetof(GpuTriangleVertex, Position);
    auto& color = vertexLayout.mAttribs[1];
    color.mSemantic = SEMANTIC_COLOR;
    color.mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
    color.mLocation = 1;
    color.mOffset = offsetof(GpuTriangleVertex, Color);

    RasterizerStateDesc rasterizer{};
    rasterizer.mCullMode = CULL_MODE_NONE;
    rasterizer.mScissor = true;
    auto format = target->mFormat;
    // Forge 没有零值 SampleCount，提交前使用颜色附件的采样数。
    GraphicsPipelineDesc graphics{}; // NOLINT(bugprone-invalid-enum-default-initialization)
    graphics.pShaderProgram = TriangleShader;
    graphics.pVertexLayout = &vertexLayout;
    graphics.pRasterizerState = &rasterizer;
    graphics.pColorFormats = &format;
    graphics.mRenderTargetCount = 1;
    graphics.mSampleCount = target->mSampleCount;
    graphics.mSampleQuality = target->mSampleQuality;
    graphics.mDepthStencilFormat = TinyImageFormat_UNDEFINED;
    graphics.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
    const PipelineDesc pipeline{
        .mGraphicsDesc = graphics,
        .pCache = nullptr,
        .pPipelineExtensions = nullptr,
        .pName = "Triangle",
        .mType = PIPELINE_TYPE_GRAPHICS,
        .mExtensionCount = 0,
#ifdef VULKAN
        .pLayouts = nullptr,
        .mLayoutCount = 0,
#endif
    };
    addPipeline(Device, &pipeline, &TrianglePipeline);
    // Forge 可能在原生创建失败后仍返回包装对象。会话关闭时由 RetireSurface 回收。
    if (TrianglePipeline == nullptr
#ifdef DIRECT3D12
        // Forge 的 D3D12 图形 Pipeline 使用该 union 成员。
        || TrianglePipeline->mDx.pPipelineState == nullptr // NOLINT(cppcoreguidelines-pro-type-union-access)
#elifdef VULKAN
        || TrianglePipeline->mVk.pPipeline == VK_NULL_HANDLE
#endif
    )
    {
        return Failure(RendererStage::kSubmit);
    }
    return {};
}

void ForgeBackend::RecordGeometry(const RenderTarget& target, FrameDraw draw) noexcept
{
    cmdSetViewport(Command, 0, 0, static_cast<float>(target.mWidth), static_cast<float>(target.mHeight), 0, 1);
    cmdSetScissor(Command, 0, 0, target.mWidth, target.mHeight);
    cmdBindPipeline(Command, TrianglePipeline);
    if (draw.Indexed)
    {
        cmdBindVertexBuffer(Command, 1, &QuadVertices, &kTriangleVertexStride, nullptr);
        cmdBindIndexBuffer(Command, QuadIndices, INDEX_TYPE_UINT16, 0);
        cmdDrawIndexed(Command, 6, 0, 0);
        return;
    }
    std::array<GpuTriangleVertex, kTriangleVertexCount> vertices{};
    std::ranges::transform(*draw.Triangle, vertices.begin(), PackVertex);
    // Acquire 已等待持有映射缓冲的上一帧命令 Fence。
    std::memcpy(TriangleBuffer->pCpuMappedAddress, vertices.data(), sizeof(vertices));
    cmdBindVertexBuffer(Command, 1, &TriangleBuffer, &kTriangleVertexStride, nullptr);
    cmdDraw(Command, kTriangleVertexCount, 0);
}

Status ForgeBackend::Submit(LinearColor color, FrameDraw draw) noexcept
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
    if (draw.Triangle != nullptr || draw.Indexed)
    {
        RecordGeometry(*target, draw);
    }
    cmdBindRenderTargets(Command, nullptr);

    std::swap(barrier.mCurrentState, barrier.mNewState);
    cmdResourceBarrier(Command, 0, nullptr, 0, nullptr, 1, &barrier);
    endCmd(Command);

    std::array waits{ImageAvailable, QuadUploadReady};
    const QueueSubmitDesc submit{
        .ppCmds = &Command,
        .pSignalFence = CommandFence,
        .ppWaitSemaphores = waits.data(),
        .ppSignalSemaphores = &ReadyToPresent.at(Image),
        .mCmdCount = 1,
        .mWaitSemaphoreCount = QuadUploadReady != nullptr ? 2U : 1U,
        .mSignalSemaphoreCount = 1,
        .mSubmitDone = false,
    };
    queueSubmit(GraphicsQueue, &submit);
    QuadUploadReady = nullptr;
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
    if (TrianglePipeline != nullptr)
    {
        removePipeline(Device, std::exchange(TrianglePipeline, nullptr));
    }
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

void ForgeBackend::CloseTriangleResources() noexcept
{
    QuadUploadReady = nullptr;
    if (QuadIndices != nullptr)
    {
        removeResource(std::exchange(QuadIndices, nullptr));
    }
    if (QuadVertices != nullptr)
    {
        removeResource(std::exchange(QuadVertices, nullptr));
    }
    if (TriangleBuffer != nullptr)
    {
        removeResource(std::exchange(TriangleBuffer, nullptr));
    }
    if (TriangleShader != nullptr)
    {
        removeShader(Device, std::exchange(TriangleShader, nullptr));
    }
    if (std::exchange(RootSignatureReady, false))
    {
        exitRootSignature(Device);
    }
    if (std::exchange(ResourceLoaderReady, false))
    {
        exitResourceLoaderInterface(Device);
    }
}

void ForgeBackend::Close() noexcept
{
    CloseTriangleResources();
    ShaderDirectoryAvailable = false;
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
