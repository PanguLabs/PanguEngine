#include <PanguApplication.h>

#include <cstdio>
#include <expected>
#include <filesystem>
#include <print>
#include <system_error>

#ifdef __ANDROID__
#include <android/log.h>

#include <chrono>
#include <format>
#include <optional>
#endif

namespace pg::app
{
Result<LoopControl> OnFrame(GraphicsContext& graphics) noexcept
{
    static unsigned presentRequests{};
    static unsigned skipped{};
    static bool usingSecondColor{};
    constexpr unsigned kMinimumPresentRequests = 120;
    constexpr LinearColor kColorA{.R = 0.05F, .G = 0.32F, .B = 0.58F, .A = 1.0F};
    constexpr LinearColor kColorB{.R = 1.0F, .G = 0.0F, .B = 0.0F, .A = 1.0F};

#ifdef __ANDROID__
    static std::optional<std::chrono::steady_clock::time_point> stopAt;
#else
    if (!usingSecondColor)
    {
        std::error_code error;
        usingSecondColor = std::filesystem::exists("second-color", error);
    }
#endif
    const auto frame = graphics.Present(usingSecondColor ? kColorB : kColorA);
    if (!frame)
    {
        std::fputs("first-frame failure=1\n", stderr);
        return std::unexpected(frame.error());
    }
    if (*frame == FrameResult::kPresentRequested)
    {
        ++presentRequests;
#ifdef __ANDROID__
        if (presentRequests == kMinimumPresentRequests)
        {
            __android_log_write(ANDROID_LOG_INFO, "PanguFirstFrame", "vulkan-preview-present-requests=120");
            stopAt = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        }
#else
        std::println("first-frame present-request-progress={}", presentRequests);
        std::fflush(stdout);
#endif
    }
    else
    {
        ++skipped;
    }
#ifdef __ANDROID__
    if (stopAt && std::chrono::steady_clock::now() >= *stopAt)
    {
        __android_log_write(
            ANDROID_LOG_INFO, "PanguFirstFrame",
            std::format("vulkan-preview-finished presents={} skipped={}", presentRequests, skipped).c_str());
        return LoopControl::kStop;
    }
#else
    std::error_code error;
    if (presentRequests >= kMinimumPresentRequests && std::filesystem::exists("stop", error))
    {
        std::println("first-frame present-requests={} skipped={} failures=0", presentRequests, skipped);
        return LoopControl::kStop;
    }
#endif
    return LoopControl::kContinue;
}
} // namespace pg::app
