#pragma once

#include "EditorService.h"
#include "FixtureModules.h"
#include "GameplayService.h"
#include "LogOutcome.h"

#include <Check.h>
#include <EngineHost.h>
#include <EngineHostModule.h>
#include <FoundationModule.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <Log.h>
#include <LogModule.h>
#include <ModuleBuiltin.h>
#include <ModuleManager.h>
#include <RegistrationContext.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace pg::fixture
{
[[nodiscard]] inline log::LogConfig ApplicationLogging(const std::filesystem::path& directory, std::byte identity)
{
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    PG_CHECK(!error, "应用日志分区目录创建失败");
    log::LogConfig config;
    config.Directory = directory.native();
    config.PartitionId.front() = identity;
    config.Required = config.Enabled;
    return config;
}
[[nodiscard]] inline bool FinishShutdown(engine::EngineHost& host, engine::HostOperationReport& report)
{
    if (host.GetState() == engine::HostState::kReady || host.GetState() == engine::HostState::kCold)
    {
        if (!host.BeginShutdown(report))
        {
            return false;
        }
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (host.GetState() != engine::HostState::kClosed && std::chrono::steady_clock::now() < deadline)
    {
        const auto progress = host.TryFinishShutdown(report);
        if (!progress && host.GetState() != engine::HostState::kClosing)
        {
            return false;
        }
        std::this_thread::yield();
    }
    PG_CHECK(host.GetState() == engine::HostState::kClosed, "应用日志/业务关闭未完成，不能离开拥有者作用域");
    return true;
}
template <class T>
[[nodiscard]] bool VerifyContexts(engine::EngineHost& host,
                                  Result<module::ScopeAccess<T>> (*acquire)(const engine::EngineHost&,
                                                                            module::ScopeHandle) noexcept,
                                  int initial, const char* name) noexcept
{
    engine::HostOperationReport report;
    const auto first = host.CreateContext(report);
    const auto second = host.CreateContext(report);
    if (!first || !second || !host.CreateWorld(*first, report) || !host.CreateWorld(*second, report))
    {
        return false;
    }
    const auto firstAccess = acquire(host, *first);
    const auto secondAccess = acquire(host, *second);
    if (!firstAccess || !secondAccess)
    {
        return false;
    }
    {
        auto firstBorrow = firstAccess->TryBorrow();
        auto secondBorrow = secondAccess->TryBorrow();
        if (!firstBorrow || !secondBorrow || (*firstBorrow)->Value != initial || (*secondBorrow)->Value != initial)
        {
            return false;
        }
        ++(*firstBorrow)->Value;
        if ((*firstBorrow)->Value != initial + 1 || (*secondBorrow)->Value != initial)
        {
            return false;
        }
        std::cout << name << " Context values: " << initial << " -> " << (*firstBorrow)->Value
                  << "; isolated=" << (*secondBorrow)->Value << '\n';
    }
    if (!host.BeginCloseScope(*first, report) || !host.TryFinishCloseScope(*first, report) || firstAccess->TryBorrow())
    {
        return false;
    }
    auto remaining = secondAccess->TryBorrow();
    return remaining && (*remaining)->Value == initial;
}

[[nodiscard]] inline int VerifyCandidates(std::span<const module::ModuleLoadCandidate> candidates,
                                          module::ModuleId dependent, bool shared,
                                          const log::LogConfig& logging) noexcept
{
    engine::EngineHost host;
    engine::HostOperationReport report;
    pg::engine::HostStartupConfig config;
    config.Logging = logging;
    config.Modules = candidates;
    if (!host.Start(config, report))
    {
        const bool failureReported = report.StartupFailure.has_value() && report.PrimaryError.has_value();
        std::cerr << "EngineHost Start failed: stage=" << static_cast<unsigned>(report.Stage) << '\n';
        if (!FinishShutdown(host, report))
        {
            std::fputs("EngineHost startup cleanup incomplete\n", stderr);
            return 8;
        }
        return failureReported ? 3 : 1;
    }
    const int result = [&]
    {
        const auto modules = host.InspectModules();
        if (!modules || modules->size() != 6)
        {
            return 1;
        }
        std::size_t residents{};
        bool physicsFound{};
        bool dependentFound{};
        for (const auto& snapshot : *modules)
        {
            if (snapshot.State != module::ModuleState::kActive)
            {
                return 5;
            }
            if (snapshot.Resident)
            {
                ++residents;
                if (snapshot.Descriptor.Id != module::kFoundationModuleId &&
                    snapshot.Descriptor.Id != module::kModuleRuntimeModuleId &&
                    snapshot.Descriptor.Id != log::kLogModuleId &&
                    snapshot.Descriptor.Id != engine::kEngineHostModuleId)
                {
                    return 2;
                }
            }
            else if (snapshot.Descriptor.Id == kPhysicsId)
            {
                physicsFound = true;
            }
            else if (snapshot.Descriptor.Id == dependent)
            {
                dependentFound = true;
            }
            else
            {
                return 4;
            }
        }
        if (residents != 4 || !physicsFound || !dependentFound)
        {
            return 4;
        }
        const auto declarations = host.InspectDeclarations();
        const auto expected = dependent == kGameplayId ? kGameplayDeclaration : kEditorDeclaration;
        // 拥有型快照保留原声明数、身份及 Physics 优先发布的顺序断言。
        if (!declarations || declarations->size() != 2 || declarations->front().Owner != kPhysicsId ||
            declarations->front().Id != kPhysicsDeclaration || declarations->back().Owner != dependent ||
            declarations->back().Id != expected)
        {
            return 6;
        }
        if (candidates.empty() || shared != std::holds_alternative<module::SharedModuleCandidate>(candidates.front()))
        {
            return 9;
        }
        const bool serviceValid = dependent == kGameplayId
                                      ? VerifyContexts(host, AcquireGameplaySession, 7, "Gameplay")
                                      : VerifyContexts(host, AcquireEditorSession, 11, "EditorExtras");
        return serviceValid ? 0 : 10;
    }();
    const auto submitted = PG_LOG_INFO(host.GetLogWriter(), "application", "Host application validated");
    // Ready 后集合固定；Scope 借用已全部结束，整体 Shutdown 替代逐业务 Unload。
    if (!FinishShutdown(host, report))
    {
        std::fputs("EngineHost shutdown incomplete\n", stderr);
        return 8;
    }
    if (!report.LogStop || !ValidateLogOutcome(submitted, *report.LogStop))
    {
        return 11;
    }
    if (submitted == log::SubmitResult::kAccepted)
    {
        std::ifstream file(std::filesystem::path(host.InspectLog().File.ActivePath.View()));
        const std::string text(std::istreambuf_iterator<char>{file}, {});
        if (!file || !text.contains("Host application validated"))
        {
            return 11;
        }
        std::puts("Log outputs complete: H=1 accepted=1 dropped=0 console=1 file=1 resources=1 complete=1");
    }
    else
    {
        std::puts("Log outputs incomplete: H=0 accepted=0 dropped=1 console=0 file=0 resources=1 complete=0");
    }
    const auto remainingModules = host.InspectModules();
    const auto remainingDeclarations = host.InspectDeclarations();
    if (!remainingModules || !remainingModules->empty() || !remainingDeclarations || !remainingDeclarations->empty())
    {
        return 8;
    }
    return result;
}

[[nodiscard]] inline int VerifyCatalog(const module::StaticModuleCatalog& catalog, module::ModuleId dependent,
                                       const log::LogConfig& logging) noexcept
{
    std::vector<module::ModuleLoadCandidate> candidates;
    for (const auto& entry : catalog.Entries)
    {
        candidates.emplace_back(module::StaticModuleCandidate{entry.Query});
    }
    return VerifyCandidates(candidates, dependent, false, logging);
}
} // namespace pg::fixture
