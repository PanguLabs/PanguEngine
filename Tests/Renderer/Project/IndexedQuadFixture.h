#pragma once

#include <PanguApplication.h>

#include <array>
#include <expected>
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
inline constexpr renderer::IndexedQuad kFixtureQuad{
    .Vertices = std::to_array<TriangleVertex>({
        {.X = -0.75F, .Y = -0.75F, .Color = {.A = 1.0F}},
        {.X = 0.75F, .Y = -0.75F, .Color = {.R = 1.0F, .A = 1.0F}},
        {.X = 0.75F, .Y = 0.75F, .Color = {.A = 1.0F}},
        {.X = -0.75F, .Y = 0.75F, .Color = {.G = 1.0F, .A = 1.0F}},
    }),
    .Indices = {0, 1, 2, 0, 2, 3},
};

template <typename Draw>
Result<LoopControl> RunIndexedQuad(GraphicsContext& graphics, Draw draw, bool observationComplete = true) noexcept
{
    static unsigned requests{};
#ifndef __ANDROID__
    static bool reopened{};
    static unsigned skipped{};
    std::error_code error;
    const bool requested = !reopened && std::filesystem::exists("reopen", error);
    if (requested)
    {
        if (const auto status = graphics.ReopenGraphics(); !status)
        {
            return std::unexpected(status.error());
        }
        reopened = true;
    }
#endif
    const auto frame = draw(graphics);
    if (!frame)
    {
        return std::unexpected(frame.error());
    }
    if (*frame == FrameResult::kPresentRequested)
    {
        ++requests;
#ifdef __ANDROID__
        const bool finished = requests >= 420 && observationComplete;
        if (requests == 120 || finished)
        {
            const auto message = requests == 120 ? std::format("vulkan-present-requests={}", requests)
                                                 : std::format("vulkan-finished presents={}", requests);
            __android_log_write(ANDROID_LOG_INFO, "PanguIndexedMesh", message.c_str());
        }
        if (finished)
        {
            return LoopControl::kStop;
        }
#else
        static bool reportedReopen{};
        if (reopened && !reportedReopen)
        {
            std::println("indexed-mesh reopened");
            reportedReopen = true;
        }
        std::println("indexed-mesh present-request-progress={}", requests);
        std::fflush(stdout);
#endif
    }
#ifndef __ANDROID__
    if (*frame != FrameResult::kPresentRequested)
    {
        ++skipped;
    }
    if (requests >= 120 && observationComplete && std::filesystem::exists("stop", error))
    {
        std::println("indexed-mesh present-requests={} skipped={} failures=0", requests, skipped);
        return LoopControl::kStop;
    }
#endif
    return LoopControl::kContinue;
}
} // namespace pg::app
