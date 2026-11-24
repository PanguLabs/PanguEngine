#pragma once

#include <Error.h>

#include <compare>
#include <cstdint>
#include <optional>

struct ANativeActivity;

namespace pg::renderer
{
class NativeSurface;

struct RendererOpenDesc
{
    // Android Vulkan 预览需要有效的 Activity。调用方须在 Open 期间保活，成功后延续至会话关闭完成。
    ANativeActivity* Activity{};
#if defined(__linux__) && !defined(__ANDROID__)
    // 已有窗口时据此选取能呈现的 GPU。仅在本次 Open 调用期间借用。
    const NativeSurface* PresentationSurface{};
#endif
};

struct LinearColor
{
    float R{};
    float G{};
    float B{};
    float A{};
};

struct PixelSize
{
    std::uint32_t Width{};
    std::uint32_t Height{};
};

class SurfaceId final
{
public:
    constexpr SurfaceId() noexcept = default;
    explicit constexpr SurfaceId(std::uint64_t value) noexcept
        : RawValue(value)
    {
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept { return RawValue != 0; }
    constexpr auto operator<=>(const SurfaceId&) const noexcept = default;

private:
    std::uint64_t RawValue{};
};

enum class FrameResult : std::uint8_t
{
    // Forge 的呈现调用已返回。
    kPresentRequested,
    kSkipped,
    kReattach,
};

enum class RendererStage : std::uint8_t
{
    kOpen = 1,
    kAttach = 2,
    kAcquire = 3,
    kSubmit = 4,
    kDrain = 7,
};

enum class RendererError : std::uint8_t
{
    kBusy = 1,
    kInvalidState = 2,
    kInvalidSurface = 3,
    kWrongThread = 4,
    kNoDevice = 5,
    kNativeFailure = 6,
    kPresentationUnavailable = 9,
};

inline constexpr ErrorDomainId kRendererErrorDomain = ErrorDomainId::Constant<0x70672e72656e64ULL>();
inline constexpr unsigned kRendererStageShift = 8;
inline constexpr std::uint32_t kRendererErrorMask = (1U << kRendererStageShift) - 1U;

[[nodiscard]] constexpr Error MakeRendererError(RendererStage stage, RendererError cause,
                                                std::optional<NativeErrorCode> native = std::nullopt) noexcept
{
    const auto code = (static_cast<std::uint32_t>(stage) << kRendererStageShift) | static_cast<std::uint32_t>(cause);
    return native ? Error{kRendererErrorDomain, code, *native} : Error{kRendererErrorDomain, code};
}

[[nodiscard]] constexpr RendererStage GetRendererStage(Error error) noexcept
{
    return static_cast<RendererStage>(error.Code() >> kRendererStageShift);
}

[[nodiscard]] constexpr RendererError GetRendererCause(Error error) noexcept
{
    return static_cast<RendererError>(error.Code() & kRendererErrorMask);
}
} // namespace pg::renderer
