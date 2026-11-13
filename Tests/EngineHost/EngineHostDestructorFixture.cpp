#include "EngineHostTestFixture.h"
#include <EngineHost.h>
#include <EngineHostTestAccess.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <HostTestState.h>
#include <Log.h>
#include <LogTestAccess.h>
#include <LogTypes.h>
#include <ModuleCodePin.h>
#include <ModuleDescriptor.h>
#include <ModuleTestAccess.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
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
    marker << "FATAL_READY\n";
    marker.close();
    if (!marker)
    {
        std::_Exit(12);
    }
}
const pg::module::ModuleDefinition* DestroyDuringQuery() noexcept
{
    // 显式销毁避免 unique_ptr::reset 先清空指针掩盖重入析构。
    SaveMarker();
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
    if (mode == "control-unprepared")
    {
        return 90;
    }
    if (mode == "control-exit42" || mode == "control-crash" || mode == "control-hang")
    {
        SaveMarker();
        if (mode == "control-exit42")
        {
            return 42;
        }
        if (mode == "control-crash")
        {
            __builtin_trap();
        }
        for (;;)
        {
            std::this_thread::yield();
        }
    }
    if (mode == "log-busy" || mode == "log-clean")
    {
        pg::log::detail::LogTestHooks hooks;
        std::jthread release;
        {
            EngineHost host;
            pg::log::detail::LogTestAccess::Attach(detail::EngineHostTestAccess::Log(host), hooks);
            HostStartupConfig config;
            config.Logging.Enabled = pg::log::OutputMask::kConsole;
            HostOperationReport report;
            if (!host.Start(config, report))
            {
                return 20;
            }
            hooks.BlockOutput.store(mode == "log-busy");
            if (PG_LOG_INFO(host.GetLogWriter(), "host", "destructor") != pg::log::SubmitResult::kAccepted)
            {
                return 21;
            }
            if (mode == "log-busy")
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (!hooks.OutputEntered.load() && std::chrono::steady_clock::now() < deadline)
                {
                    std::this_thread::yield();
                }
                if (!hooks.OutputEntered.load())
                {
                    std::_Exit(22);
                }
                SaveMarker();
            }
            else
            {
                hooks.BlockExit.store(true);
                if (!host.BeginShutdown(report) || host.TryFinishShutdown(report) || !report.WaitingForLog)
                {
                    std::_Exit(24);
                }
                release = std::jthread(
                    [&hooks]
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        hooks.BlockExit.store(false);
                    });
            }
        }
        if (hooks.WorkerHandles.load() != 0 || hooks.DataBlocks.load() != 0)
        {
            return 23;
        }
        SaveMarker();
        return 0;
    }
    pg::module::ModuleCodePin pin;
    if (mode == "operating")
    {
        EngineHost host;
        Context().OperatingHost = &host;
        const std::array queries{DestroyDuringQuery};
        HostStartupConfig config;
        config.Logging = pg::engine::test::DisabledLogging();
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
    if (mode != "clean")
    {
        // 在真实业务 Destroy 后记录证据，Fatal 不运行任意测试回调。
        Context().Observed->OnDestroy = SaveMarker;
    }
    // clean 同时验证显式完成与析构兜底各一轮。
    for (int iteration = 0; iteration < (mode == "clean" ? 2 : 1); ++iteration)
    {
        EngineHost host;
        HostOperationReport report;
        const auto candidates = test::HostCandidates();
        HostStartupConfig config;
        config.Logging = pg::engine::test::DisabledLogging();
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
