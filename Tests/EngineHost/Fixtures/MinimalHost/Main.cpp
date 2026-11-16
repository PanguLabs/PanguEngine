#include "MinimalService.h"
#include <Check.h>
#include <EngineHost.h>
#include <HostMetaAccess.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <Log.h>
#include <LogTypes.h>
#include <MinimalService.gen.h>
#include <ModuleManager.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <thread>
#ifdef PG_SAMPLE_MODULAR
#include <span>
#else
#include <GeneratedModuleCatalog.h>
#endif

namespace
{
[[nodiscard]] bool Shutdown(pg::engine::EngineHost& host)
{
    pg::engine::HostOperationReport report;
    if (host.GetState() == pg::engine::HostState::kCold || host.GetState() == pg::engine::HostState::kReady)
    {
        PG_CHECK(host.BeginShutdown(report), "样例无法开始关闭 Host");
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!host.TryFinishShutdown(report) && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    PG_CHECK(host.GetState() == pg::engine::HostState::kClosed, "样例有界关闭仍未完成，不能销毁持有资源的 Host");
    const bool released = report.LogStop && report.LogStop->ResourcesReleased;
    const bool complete = report.LogStop && report.LogStop->FinalFlush.OutputComplete;
    std::cout << "Log resources released=" << released << "; output complete=" << complete << '\n';
    return released && complete;
}
[[nodiscard]] bool UseCounters(pg::engine::EngineHost& host)
{
    pg::engine::HostOperationReport report;
    const auto first = host.CreateContext(report);
    const auto second = host.CreateContext(report);
    if (!first || !second || !host.CreateWorld(*first, report) || !host.CreateWorld(*second, report))
    {
        return false;
    }
    const auto firstAccess = pg::sample::AcquireCounter(host, *first);
    const auto secondAccess = pg::sample::AcquireCounter(host, *second);
    if (!firstAccess || !secondAccess)
    {
        return false;
    }
    {
        auto firstBorrow = firstAccess->TryBorrow();
        auto secondBorrow = secondAccess->TryBorrow();
        if (!firstBorrow || !secondBorrow || (*firstBorrow)->Value != 7 || (*secondBorrow)->Value != 7)
        {
            return false;
        }
        const auto& valueField = std::get<0>(pg::meta::TypeTraits<pg::sample::CounterService>::Fields);
        valueField.Write(**firstBorrow, valueField.Read(**firstBorrow) + 1);
        if ((*firstBorrow)->Value != 8 || (*secondBorrow)->Value != 7)
        {
            return false;
        }
        std::cout << "Counter Context values: 7 -> " << (*firstBorrow)->Value << "; isolated=" << (*secondBorrow)->Value
                  << '\n';
    }
    // Borrow 已离开作用域；关闭 Context 同时关闭其 World，另一个 Context 仍可访问。
    if (!host.BeginCloseScope(*first, report) || !host.TryFinishCloseScope(*first, report) || firstAccess->TryBorrow())
    {
        return false;
    }
    auto remaining = secondAccess->TryBorrow();
    return remaining && (*remaining)->Value == 7;
}
} // namespace

// main 保持标准入口签名；参数仅作为只读 span 使用。
// NOLINTNEXTLINE(misc-const-correctness)
int main(int argc, char** argv)
{
#ifdef PG_SAMPLE_MODULAR
    const std::span<char* const> arguments(argv, static_cast<std::size_t>(argc));
    if (argc != 2 || !std::filesystem::path(arguments.back()).is_absolute())
    {
        std::fputs("用法：PanguMinimalHost <服务动态库的绝对路径>\n", stderr);
        return 1;
    }
    const std::array<pg::module::ModuleLoadCandidate, 1> candidates{
        pg::module::SharedModuleCandidate{.ExpectedId = pg::sample::kModuleId, .Path = arguments.back()},
    };
#else
    (void)argv;
    if (argc != 1)
    {
        std::fputs("Monolithic 示例不接受动态库路径。\n", stderr);
        return 1;
    }
    const auto catalog = pg::module::GeneratedStaticModuleCatalog();
    const std::array<pg::module::ModuleLoadCandidate, 1> candidates{
        pg::module::StaticModuleCandidate{catalog.Entries.front().Query},
    };
#endif
    std::error_code directoryError;
    auto directory = std::filesystem::current_path(directoryError);
    if (directoryError)
    {
        return 5;
    }
    directory /= "MinimalHostLogs";
    std::filesystem::create_directories(directory, directoryError);
    if (directoryError)
    {
        std::fputs("Application 无法创建日志专属目录。\n", stderr);
        return 5;
    }
    pg::engine::EngineHost host;
    pg::engine::HostOperationReport report;
    pg::engine::HostStartupConfig config;
    config.Modules = candidates;
    config.Logging.Directory = directory.native();
    config.Logging.PartitionId.front() = std::byte{0x73};
    config.Logging.Required = pg::log::OutputMask::kFile;
    if (!host.Start(config, report))
    {
        std::cerr << "Start 失败：stage=" << static_cast<unsigned>(report.Stage)
                  << ", startupFailure=" << report.StartupFailure.has_value()
                  << ", primaryError=" << report.PrimaryError.has_value() << '\n';
        const bool closed = Shutdown(host);
        return closed && report.StartupFailure && report.PrimaryError ? 2 : 3;
    }
    PG_LOG_INFO(host.GetLogWriter(), "sample", "Host ready");
    bool valid = UseCounters(host);
    {
        auto description = pg::meta::BorrowTypeInfo<pg::sample::CounterService>(host);
        valid = valid && description && description->Get()->Fields.size() == 1;
        if (description && description->Get()->Fields.size() == 1)
        {
            std::cout << "Generated metadata: " << description->Get()->Name
                      << "; field=" << description->Get()->Fields.front().Name << '\n';
        }
    }
    const bool closed = Shutdown(host);
    if (!valid || !closed)
    {
        std::fputs("服务值、Context 隔离或显式关闭验证失败。\n", stderr);
        return 4;
    }
    std::puts("MinimalHost shutdown complete");
    return 0;
}
