#include <ModuleDescriptor.h>
#include <RendererModule.h>

#include <cstdlib>

#ifdef PG_RENDERER_IOS_TRIANGLE_LINK_CHECK
#include <EngineHost.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <LogTypes.h>
#include <RendererAccess.h>
#include <RendererSession.h>
#include <RendererTypes.h>

#include <array>

namespace
{
[[nodiscard]] bool CheckTriangleEntry(const pg::engine::EngineHost& host) noexcept
{
    const auto renderer = pg::renderer::AcquireRenderer(host);
    if (!renderer)
    {
        return false;
    }
    const auto session = renderer->TryBorrow();
    if (!session)
    {
        return false;
    }
    // 冷会话允许检查真实入口，无需 UIKit 窗口和 Metal 设备。
    constexpr pg::renderer::LinearColor kClearColor{.A = 1.0F};
    constexpr std::array<pg::renderer::TriangleVertex, 3> kVertices{};
    constexpr auto kExpectedError = pg::renderer::MakeRendererError(pg::renderer::RendererStage::kAcquire,
                                                                    pg::renderer::RendererError::kInvalidState);
    const auto frame = (*session)->PresentTriangle(kClearColor, kVertices);
    const auto indexed = (*session)->PresentIndexedMesh(kClearColor);
    const auto transformed = (*session)->PresentIndexedMesh(kClearColor, pg::renderer::MeshTransform{});
    // 越界索引在访问设备前被拒绝，使探针同时覆盖带几何的打开路径。
    constexpr pg::renderer::IndexedQuad kInvalidQuad{.Indices = {0, 1, 2, 0, 2, 4}};
    const auto open = (*session)->Open({.Quad = &kInvalidQuad});
    return !frame && frame.error() == kExpectedError && !indexed && indexed.error() == kExpectedError && !transformed &&
           transformed.error() == kExpectedError && !open &&
           open.error() == pg::renderer::MakeRendererError(pg::renderer::RendererStage::kOpen,
                                                           pg::renderer::RendererError::kInvalidVertex);
}
} // namespace
#endif

int main()
{
    // 引用公开 Query，确保最终链接包含真实 Renderer Module。
    const auto* definition = pg::renderer::QueryRendererModule();
    if (definition == nullptr || definition->Descriptor.Id != pg::renderer::kRendererModuleId)
    {
        return EXIT_FAILURE;
    }
#ifdef PG_RENDERER_IOS_TRIANGLE_LINK_CHECK
    const std::array<pg::module::QueryModuleFn, 1> queries{pg::renderer::QueryRendererModule};
    const pg::engine::HostStartupConfig config{
        .AdditionalResidentQueries = queries,
        .ResidentBindings = {},
        .Modules = {},
        .ModuleBindings = {},
        .Logging = {.Directory = {}, .Enabled = pg::log::OutputMask::kNone},
    };
    pg::engine::EngineHost host;
    pg::engine::HostOperationReport report;
    if (!host.Start(config, report))
    {
        return EXIT_FAILURE;
    }
    const auto triangle = CheckTriangleEntry(host);
    const auto begun = host.BeginShutdown(report);
    const auto closed = host.TryFinishShutdown(report);
    return triangle && begun && closed ? EXIT_SUCCESS : EXIT_FAILURE;
#else
    return EXIT_SUCCESS;
#endif
}
