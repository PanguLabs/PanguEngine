#include "EngineHost.h"
#include "HostOperation.h"
#include "HostTestState.h"
#include "MetaActor.h"
#include "MetaTestFixture.h"
#include <HostMetaAccess.h>
#include <ModuleError.h>
#include <cstddef>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>

// main 参数通过只读 span 访问。
// NOLINTNEXTLINE(misc-const-correctness)
int main(int argc, char** argv)
{
    if (argc != 3)
    {
        return 2;
    }
    using namespace pg::test::meta_fixture;
    const std::span arguments(argv, static_cast<std::size_t>(argc));
    std::ofstream marker(arguments.subspan(1).front());
    const auto state = std::make_shared<pg::engine::test::HostTestState>();
    state->OnNativeUnload = [&marker]
    {
        marker << "NativeUnload\n";
        marker.flush();
    };
    pg::engine::test::SetHostTestState(state);
    pg::engine::EngineHost host;
    pg::engine::HostOperationReport report;
    const std::string_view mode(arguments.subspan(2).front());
    const auto started = Start(host, report);
    if (mode == "invalid-provider")
    {
        const bool valid =
            !started && started.error() == pg::module::MakeModuleError(pg::module::ModuleError::kInvalidDefinition);
        const auto closed = Shutdown(host, report);
        state->OnNativeUnload = {};
        pg::engine::test::SetHostTestState({});
        return valid && closed ? 0 : 10;
    }
    if (!started)
    {
        return 3;
    }
    auto info = pg::meta::BorrowTypeInfo<Actor>(host);
    if (mode == "no-provider")
    {
        const bool valid = !info && info.error() == pg::module::MakeModuleError(pg::module::ModuleError::kNotFound);
        if (info)
        {
            info->Release();
        }
        const auto closed = Shutdown(host, report);
        state->OnNativeUnload = {};
        pg::engine::test::SetHostTestState({});
        return valid && closed ? 0 : 11;
    }
    if (!info)
    {
        const auto closed = Shutdown(host, report);
        return closed ? 4 : 13;
    }
    const std::string name(info->Get()->Name);
    if (info->Get()->Fields.size() != 8)
    {
        return 12;
    }
    const std::string key(info->Get()->Fields.subspan(1).front().Key);
    if (!host.BeginShutdown(report) || host.TryFinishShutdown(report) || state->NativeUnloadCount != 0)
    {
        return 5;
    }
    info->Release();
    if (!host.TryFinishShutdown(report))
    {
        return 6;
    }
#ifdef PG_TEST_MODULAR
    if (state->NativeUnloadCount != 1)
    {
        return 7;
    }
#endif
    if (name != "pg::test::meta_fixture::Actor" || key != "health")
    {
        return 8;
    }
    marker << "OwnedDiagnosticsRead\n";
    state->OnNativeUnload = {};
    pg::engine::test::SetHostTestState({});
    return marker ? 0 : 9;
}
