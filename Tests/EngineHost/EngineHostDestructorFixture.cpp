#include "EngineHostTestFixture.h"
#include <EngineHost.h>
#include <EngineHostTestAccess.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <HostTestState.h>
#include <ModuleCodePin.h>
#include <ModuleDescriptor.h>
#include <ModuleTestAccess.h>
#include <array>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace
{
using namespace pg::engine;
struct ProcessContext
{
    std::shared_ptr<test::HostTestState> Observed;
    const char* MarkerPath{};
    EngineHost* OperatingHost{};
};
ProcessContext& Context()
{
    static ProcessContext context;
    return context;
}
void SaveMarker()
{
    std::ofstream marker(Context().MarkerPath);
    marker << "NativeUnloadCount=" << Context().Observed->NativeUnloadCount << '\n';
    marker << "DestroyCount=" << Context().Observed->DestroyCount << '\n';
    marker.close();
    if (!marker)
    {
        std::_Exit(12);
    }
}
const pg::module::ModuleDefinition* DestroyDuringQuery() noexcept
{
    // 显式销毁避免 unique_ptr::reset 先清空指针掩盖重入析构。
    Context().OperatingHost->~EngineHost();
    std::_Exit(10);
}
} // namespace
// main 的标准参数签名保留可变字符指针。
// NOLINTNEXTLINE(misc-const-correctness)
int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        return 2;
    }
    const std::span arguments(argv, static_cast<std::size_t>(argc));
    const std::string_view mode(arguments.subspan(1).front());
    Context().MarkerPath = arguments.subspan(2).front();
    Context().Observed = std::make_shared<test::HostTestState>();
    test::SetHostTestState(Context().Observed);
    std::set_terminate(
        []
        {
            SaveMarker();
            std::_Exit(91);
        });
    pg::module::ModuleCodePin pin;
    if (mode == "operating")
    {
        EngineHost host;
        Context().OperatingHost = &host;
        const std::array queries{DestroyDuringQuery};
        HostStartupConfig config;
        config.AdditionalResidentQueries = queries;
        HostOperationReport report;
        const auto result = host.Start(config, report);
        Context().OperatingHost = nullptr;
        return result ? 10 : 9;
    }
    if (mode != "clean" && mode != "busy" && mode != "once")
    {
        return 3;
    }
    // clean 同时验证显式完成与析构兜底各一轮。
    for (int iteration = 0; iteration < (mode == "clean" ? 2 : 1); ++iteration)
    {
        EngineHost host;
        HostOperationReport report;
        const auto candidates = test::HostCandidates();
        HostStartupConfig config;
        config.Modules = candidates;
        if (!host.Start(config, report))
        {
            return 4;
        }
        auto* manager = detail::EngineHostTestAccess::Manager(host);
        if (manager == nullptr)
        {
            return 5;
        }
        const auto handle = manager->Modules().back();
        if (mode == "busy")
        {
            auto acquired = manager->TryPinForBusiness(handle);
            if (!acquired)
            {
                return 6;
            }
            pin = std::move(*acquired);
        }
        if (mode == "once")
        {
#ifdef PG_TEST_MODULAR
            pg::module::detail::ModuleManagerTestAccess::FailNextNativeClose(*manager, handle);
#else
            return 7;
#endif
        }
        if (mode == "clean" && iteration == 0)
        {
            if (!host.BeginShutdown(report) || !host.TryFinishShutdown(report))
            {
                return 8;
            }
        }
    }
    if (mode != "clean")
    {
        return 11;
    }
    SaveMarker();
    return 0;
}
