#include <EngineHost.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <LogTypes.h>
#include <ModuleDescriptor.h>
#include <NativeSurface.h>
#include <RendererAccess.h>
#include <RendererModule.h>
#include <RendererSession.h>
#include <RendererTypes.h>
#include <gtest/gtest.h>
#include <windows.h> // IWYU pragma: keep

#include <minwindef.h>
#include <winuser.h>

#include <array>

namespace pg::renderer
{
namespace
{
TEST(Host, RendererOwnedByExactHost)
{
    const std::array<module::QueryModuleFn, 1> queries{QueryRendererModule};
    engine::HostStartupConfig config{
        .AdditionalResidentQueries = {},
        .ResidentBindings = {},
        .Modules = {},
        .ModuleBindings = {},
        .Logging = {.Directory = {}, .Enabled = log::OutputMask::kNone},
    };
    engine::EngineHost headless;
    engine::EngineHost first;
    engine::EngineHost second;
    engine::HostOperationReport report;
    ASSERT_TRUE(headless.Start(config, report));
    EXPECT_FALSE(AcquireRenderer(headless));
    config.AdditionalResidentQueries = queries;
    ASSERT_TRUE(first.Start(config, report));
    ASSERT_TRUE(second.Start(config, report));

    const auto firstAccess = AcquireRenderer(first);
    const auto secondAccess = AcquireRenderer(second);
    ASSERT_TRUE(firstAccess);
    ASSERT_TRUE(secondAccess);
    auto firstBorrow = firstAccess->TryBorrow();
    auto secondBorrow = secondAccess->TryBorrow();
    ASSERT_TRUE(firstBorrow);
    ASSERT_TRUE(secondBorrow);
    EXPECT_NE(firstBorrow->Get(), secondBorrow->Get());
    EXPECT_FALSE(firstBorrow->Get()->Present({}));
    ASSERT_TRUE(firstBorrow->Get()->Open());

    // 已撤销的 HWND 仍是非空句柄，交给 Forge 前必须判定无效。
    auto* const window =
        CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, nullptr, nullptr);
    ASSERT_NE(window, nullptr);
    ASSERT_NE(DestroyWindow(window), FALSE);
    constexpr SurfaceId kStaleSurface{1};
    const auto attached = firstBorrow->Get()->Attach(kStaleSurface, NativeSurface{Win32Surface{window}},
                                                     PixelSize{.Width = 64, .Height = 64});
    ASSERT_FALSE(attached);
    EXPECT_EQ(GetRendererStage(attached.error()), RendererStage::kAttach);
    EXPECT_EQ(GetRendererCause(attached.error()), RendererError::kInvalidSurface);
    ASSERT_TRUE(firstBorrow->Get()->Detach(kStaleSurface));

    const auto busy = secondBorrow->Get()->Open();
    ASSERT_FALSE(busy);
    EXPECT_EQ(GetRendererCause(busy.error()), RendererError::kBusy);
    ASSERT_TRUE(firstBorrow->Get()->Close());
    firstBorrow->Release();
    ASSERT_TRUE(first.BeginShutdown(report));
    ASSERT_TRUE(first.TryFinishShutdown(report));
    EXPECT_FALSE(firstAccess->TryBorrow());
    EXPECT_FALSE(AcquireRenderer(headless));

    ASSERT_TRUE(secondBorrow->Get()->Open());
    ASSERT_TRUE(secondBorrow->Get()->Close());
    secondBorrow->Release();
    ASSERT_TRUE(second.BeginShutdown(report));
    ASSERT_TRUE(second.TryFinishShutdown(report));
    ASSERT_TRUE(headless.BeginShutdown(report));
    ASSERT_TRUE(headless.TryFinishShutdown(report));
}
} // namespace
} // namespace pg::renderer
