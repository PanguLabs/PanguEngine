#include "IndexedQuadFixture.h"

#include <PanguApplication.h>
#include <RendererTypes.h>
#include <Result.h>

#ifdef __ANDROID__
#include <array>
#include <sys/system_properties.h>
#else
#include <filesystem>
#include <system_error>
#endif

namespace pg::app
{
constexpr renderer::IndexedQuad kIndexedQuad = kFixtureQuad;

namespace
{
renderer::MeshTransform Translation(float x) noexcept
{
    return {.Elements = {0.4F, 0, 0, 0, 0, 0.4F, 0, 0, 0, 0, 1, 0, x, 0, 0, 1}};
}

#ifndef __ANDROID__
renderer::MeshTransform CurrentTransform() noexcept
{
    constexpr renderer::MeshTransform kIdentity{.Elements = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    std::error_code error;
    if (std::filesystem::exists("reopen", error))
    {
        return kIdentity;
    }
    if (std::filesystem::exists("right", error))
    {
        return Translation(0.5F);
    }
    if (std::filesystem::exists("left", error))
    {
        return Translation(-0.5F);
    }
    if (std::filesystem::exists("perspective", error))
    {
        return {.Elements = {1, 0, 0, 0.5F, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    }
    if (std::filesystem::exists("rotate", error))
    {
        return {.Elements = {0, 0.5F, 0, 0, -0.25F, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    }
    return kIdentity;
}
#endif
} // namespace

Result<LoopControl> OnFrame(GraphicsContext& graphics) noexcept
{
#ifdef __ANDROID__
    std::array<char, PROP_VALUE_MAX> phase{};
    __system_property_get("debug.pangu.mesh_pose", phase.data());
    const auto value = phase.front();
    const auto transform = Translation(value == 'R' || value == 'D' ? 0.5F : -0.5F);
    const bool observationComplete = value != 'L' && value != 'R';
#else
    const auto transform = CurrentTransform();
    const bool observationComplete = true;
#endif
    return RunIndexedQuad(
        graphics, [&transform](GraphicsContext& context) { return context.PresentIndexedMesh({.A = 1.0F}, transform); },
        observationComplete);
}
} // namespace pg::app
