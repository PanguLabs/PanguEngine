#include "EngineHostTestFixture.h"
#include "HostServiceModule.h"
#include <EngineHost.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <HostTestState.h>
#include <ModuleError.h>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <span>

// 标记仅观测原生析构；不会持有 ScopeAccess 或业务对象。
// NOLINTNEXTLINE(misc-const-correctness)
int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        return 2;
    }
    using namespace pg::engine;
    const std::span arguments(argv, static_cast<std::size_t>(argc));
    std::ofstream marker(arguments.subspan(1).front());
    if (!marker)
    {
        return 3;
    }
    const auto state = std::make_shared<test::HostTestState>();
    test::SetHostTestState(state);
    state->OnNativeUnload = [&marker]
    {
        marker << "NativeUnload\n";
        marker.flush();
    };
    EngineHost host;
    HostOperationReport report;
    const auto candidates = test::HostCandidates();
    HostStartupConfig config;
    config.Modules = candidates;
    if (!host.Start(config, report))
    {
        std::_Exit(4);
    }
    const auto context = host.CreateContext(report);
    if (!context)
    {
        std::_Exit(5);
    }
    const auto world = host.CreateWorld(*context, report);
    if (!world)
    {
        std::_Exit(6);
    }
    auto contextAccess = test::AcquireContextValue(host, *context);
    auto worldAccess = test::AcquireWorldValue(host, *world);
    if (!contextAccess || !worldAccess)
    {
        std::_Exit(7);
    }
    auto contextBorrow = contextAccess->TryBorrow();
    auto worldBorrow = worldAccess->TryBorrow();
    if (!contextBorrow || !worldBorrow || (*contextBorrow)->Value != 2 || (*worldBorrow)->Value != 3)
    {
        std::_Exit(8);
    }
    contextBorrow->Release();
    worldBorrow->Release();
    if (!host.BeginShutdown(report) || !host.TryFinishShutdown(report) || host.GetState() != HostState::kClosed)
    {
        std::_Exit(9);
    }
    if (state->NativeUnloadCount != 1 || state->DestroyCount != 4)
    {
        std::_Exit(10);
    }
    // 必须在 DLL 的原生析构已被观测之后首次复制残留令牌。
    auto contextCopy = *contextAccess;
    auto worldCopy = *worldAccess;
    const auto contextDenied = contextCopy.TryBorrow();
    const auto worldDenied = worldCopy.TryBorrow();
    const auto expected = pg::module::MakeModuleError(pg::module::ModuleError::kNotReady);
    if (contextDenied || worldDenied || contextDenied.error() != expected || worldDenied.error() != expected)
    {
        std::_Exit(11);
    }
    *contextAccess = {};
    *worldAccess = {};
    contextCopy = {};
    worldCopy = {};
    marker << "TokensReleased\n";
    marker.close();
    state->OnNativeUnload = {};
    return marker ? 0 : 12;
}
