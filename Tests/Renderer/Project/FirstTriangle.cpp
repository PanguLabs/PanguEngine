#include <PanguApplication.h>

#include <array>
#include <expected>
#include <utility>

#ifdef __ANDROID__
#include <android/log.h>

#include <format>
#else
#include <cstdio>
#include <filesystem>
#include <print>
#include <system_error>
#endif

namespace pg::app
{
namespace
{
constexpr unsigned kMinimumPresentRequests = 120;
constexpr LinearColor kBackground{.A = 1.0F};
constexpr LinearColor kRed{.R = 1.0F, .A = 1.0F};
constexpr LinearColor kGreen{.G = 1.0F, .A = 1.0F};
constexpr LinearColor kBlue{.B = 1.0F, .A = 1.0F};
constexpr auto kInitial = std::to_array<TriangleVertex>({
    {.X = -0.875F, .Y = -0.5F, .Color = kRed},
    {.X = -0.125F, .Y = -0.5F, .Color = kGreen},
    {.X = -0.5F, .Y = 0.5F, .Color = kBlue},
});
#ifdef __ANDROID__
constexpr const char* kLogTag = "PanguFirstTriangle";
constexpr unsigned kUpdateAfterPresentRequests = 240;
constexpr unsigned kStopAfterPresentRequests = 420;
#endif
constexpr auto kUpdated = []
{
    auto triangle = kInitial;
    for (auto& vertex : triangle)
    {
        vertex.X += 1.0F;
    }
    triangle.at(0).Color = kBlue;
    triangle.at(1).Color = kRed;
    triangle.at(2).Color = kGreen;
    return triangle;
}();
} // namespace

Result<LoopControl> OnFrame(GraphicsContext& graphics) noexcept
{
    static unsigned presentRequests{};
    static unsigned skipped{};
#ifdef __ANDROID__
    const auto& vertices = presentRequests < kUpdateAfterPresentRequests ? kInitial : kUpdated;
#else
    static bool usingUpdatedVertices{};

    std::error_code error;
    if (!usingUpdatedVertices)
    {
        usingUpdatedVertices = std::filesystem::exists("second-triangle", error);
    }
    const auto& vertices = usingUpdatedVertices ? kUpdated : kInitial;
#endif
    const auto frame = graphics.PresentTriangle(kBackground, vertices);
    if (!frame)
    {
        const auto failure = frame.error();
#ifdef __ANDROID__
        __android_log_write(ANDROID_LOG_ERROR, kLogTag,
                            std::format("vulkan-failed stage={} cause={}",
                                        std::to_underlying(renderer::GetRendererStage(failure)),
                                        std::to_underlying(renderer::GetRendererCause(failure)))
                                .c_str());
#else
        std::println(stderr, "first-triangle failure stage={} cause={}",
                     std::to_underlying(renderer::GetRendererStage(failure)),
                     std::to_underlying(renderer::GetRendererCause(failure)));
#endif
        return std::unexpected(failure);
    }
    if (*frame == FrameResult::kPresentRequested)
    {
        ++presentRequests;
#ifdef __ANDROID__
        if (presentRequests == kMinimumPresentRequests)
        {
            __android_log_write(ANDROID_LOG_INFO, kLogTag,
                                std::format("vulkan-present-requests={}", presentRequests).c_str());
        }
#else
        std::println("first-triangle present-request-progress={}", presentRequests);
        std::fflush(stdout);
#endif
    }
    else
    {
        ++skipped;
    }
#ifdef __ANDROID__
    if (presentRequests == kStopAfterPresentRequests)
    {
        __android_log_write(ANDROID_LOG_INFO, kLogTag,
                            std::format("vulkan-finished presents={} skipped={}", presentRequests, skipped).c_str());
        return LoopControl::kStop;
    }
#else
    if (presentRequests >= kMinimumPresentRequests && std::filesystem::exists("stop", error))
    {
        std::println("first-triangle present-requests={} skipped={} failures=0", presentRequests, skipped);
        return LoopControl::kStop;
    }
#endif
    return LoopControl::kContinue;
}
} // namespace pg::app
