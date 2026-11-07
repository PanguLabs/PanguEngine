#include "EngineHost.h"
#include "EngineHostState.h"
#include "EngineHostTestAccess.h"
#include "Error.h"
#include "HostError.h"
#include "HostOperation.h"
#include "HostStartupConfig.h"
#include "ModuleDescriptor.h"
#include "ModuleManager.h"
#include "ModuleOperation.h"
#include "RegistrationContext.h"
#include "Result.h"
#include "ScopeIdentity.h"
#include <exception>

#include <EngineHostModule.h>
#include <ModuleBuiltin.h>
#include <ModuleError.h>
#include <expected>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace pg::engine
{
EngineHost::EngineHost()
    : Data(std::make_unique<State>())
{
}
EngineHost::~EngineHost()
{
    if (Data->Operating)
    {
        std::terminate();
    }
    HostOperationReport report;
    // 兜底至多推进一轮；失败立即终止，不能落入 Manager 析构再清理一次。
    if (!BeginShutdown(report) || !TryFinishShutdown(report))
    {
        std::terminate();
    }
}
module::ModuleManager* detail::EngineHostTestAccess::Manager(EngineHost& host) noexcept
{
    return host.Data->Manager ? &*host.Data->Manager : nullptr;
}
HostState EngineHost::GetState() const noexcept { return Data->Value; }
module::ScopeHandle EngineHost::HostScope() const noexcept
{
    return Data->Manager ? Data->Manager->HostScope() : module::ScopeHandle{};
}
Result<std::vector<module::ModuleSnapshot>> EngineHost::InspectModules() const noexcept
{
    if (Data->Operating)
    {
        return std::unexpected(MakeHostError(HostError::kBusy));
    }
    std::vector<module::ModuleSnapshot> snapshots;
    if (Data->Manager)
    {
        for (const auto handle : Data->Manager->Modules())
        {
            auto snapshot = Data->Manager->Inspect(handle);
            if (!snapshot)
            {
                return std::unexpected(snapshot.error());
            }
            snapshots.push_back(std::move(*snapshot));
        }
    }
    return snapshots;
}
Result<std::vector<module::RegisteredDeclaration>> EngineHost::InspectDeclarations() const noexcept
{
    if (Data->Operating)
    {
        return std::unexpected(MakeHostError(HostError::kBusy));
    }
    const auto published = Data->Declarations.Declarations();
    return std::vector<module::RegisteredDeclaration>(published.begin(), published.end());
}

Status EngineHost::State::FailStartup(HostOperationReport& report, HostOperationStage stage, pg::Error cause,
                                      std::optional<module::ModuleOperationReport> moduleReport) noexcept
{
    StartupFailure = HostStartupFailure{.Stage = stage, .Cause = cause, .ModuleReport = moduleReport};
    Value = HostState::kClosing;
    if (Manager)
    {
        auto& cleanup = moduleReport.emplace();
        if (Manager->BeginShutdown(cleanup) && Manager->TryFinishShutdown(cleanup))
        {
            Manager.reset();
            Value = HostState::kClosed;
        }
    }
    else
    {
        Value = HostState::kClosed;
    }
    auto result = Report(stage);
    result.PrimaryError = cause;
    result.ModuleReport = moduleReport;
    report = result;
    return std::unexpected(cause);
}

Status EngineHost::State::StartBatch(module::ModuleManager& manager, bool resident,
                                     HostOperationReport& report) noexcept
{
    const auto order = manager.RegistrationOrder();
    const auto registration =
        resident ? HostOperationStage::kResidentRegistration : HostOperationStage::kBusinessRegistration;
    const auto activation =
        resident ? HostOperationStage::kResidentActivation : HostOperationStage::kBusinessActivation;
    for (const auto stage : {registration, activation})
    {
        const bool activate = stage == activation;
        for (const auto handle : order)
        {
            const auto snapshot = manager.Inspect(handle);
            if (!snapshot)
            {
                return FailStartup(report, stage, snapshot.error(), std::nullopt);
            }
            if (snapshot->Resident != resident)
            {
                continue;
            }
            module::ModuleOperationReport moduleReport;
            const auto result =
                activate ? manager.Activate(handle, moduleReport) : manager.Register(handle, moduleReport);
            if (!result)
            {
                return FailStartup(report, stage, result.error(), moduleReport);
            }
        }
    }
    return {};
}

Status EngineHost::Start(const HostStartupConfig& config, HostOperationReport& report) noexcept
{
    if (Data->Operating)
    {
        return std::unexpected(MakeHostError(HostError::kBusy));
    }
    if (Data->Value != HostState::kCold)
    {
        return Data->Reject(report, HostError::kInvalidState);
    }
    const State::Operation operation(*Data);
    Data->Value = HostState::kStarting;
    const auto builtins = module::BuiltinResidentQueries();
    const auto residentCount = builtins.size() + 1;
    constexpr auto kMaxBindings = module::kMaxCatalogModules * module::kMaxModuleDependencies;
    if (config.AdditionalResidentQueries.size() > module::kMaxCatalogModules - residentCount ||
        config.ResidentBindings.size() > kMaxBindings)
    {
        return Data->FailStartup(report, HostOperationStage::kResidentAdmission,
                                 module::MakeModuleError(module::ModuleError::kLimitExceeded), std::nullopt);
    }
    std::vector<module::QueryModuleFn> residents(builtins.begin(), builtins.end());
    residents.push_back(QueryEngineHostModule);
    residents.insert(residents.end(), config.AdditionalResidentQueries.begin(), config.AdditionalResidentQueries.end());
    module::ModuleOperationReport admission;
    auto manager = module::ModuleManager::Create(Data->Declarations, residents, config.ResidentBindings, admission);
    if (!manager)
    {
        return Data->FailStartup(report, HostOperationStage::kResidentAdmission, manager.error(), admission);
    }
    auto& activeManager = Data->Manager.emplace(std::move(*manager));
    const auto residentStarted = Data->StartBatch(activeManager, true, report);
    if (!residentStarted)
    {
        return residentStarted;
    }
    if (config.Modules.size() > module::kMaxCatalogModules - residents.size() ||
        config.ModuleBindings.size() > kMaxBindings)
    {
        return Data->FailStartup(report, HostOperationStage::kBusinessAdmission,
                                 module::MakeModuleError(module::ModuleError::kLimitExceeded), std::nullopt);
    }
    if (!config.Modules.empty())
    {
        const auto loaded = activeManager.Load(config.Modules, config.ModuleBindings, admission);
        if (!loaded)
        {
            return Data->FailStartup(report, HostOperationStage::kBusinessAdmission, loaded.error(), admission);
        }
        const auto businessStarted = Data->StartBatch(activeManager, false, report);
        if (!businessStarted)
        {
            return businessStarted;
        }
    }
    Data->Value = HostState::kReady;
    report = Data->Report(config.Modules.empty() ? HostOperationStage::kResidentActivation
                                                 : HostOperationStage::kBusinessActivation);
    return {};
}

Status EngineHost::BeginShutdown(HostOperationReport& report) noexcept
{
    if (Data->Operating)
    {
        return std::unexpected(MakeHostError(HostError::kBusy));
    }
    const State::Operation operation(*Data);
    auto result = Data->Report(HostOperationStage::kShutdown);
    if (!Data->Manager)
    {
        Data->Value = HostState::kClosed;
    }
    else
    {
        Data->Value = HostState::kClosing;
        module::ModuleOperationReport moduleReport;
        const auto status = Data->Manager->BeginShutdown(moduleReport);
        result.ModuleReport = moduleReport;
        if (!status)
        {
            result.State = Data->Value;
            result.PrimaryError = status.error();
            report = result;
            return status;
        }
    }
    result.State = Data->Value;
    report = result;
    return {};
}

Status EngineHost::TryFinishShutdown(HostOperationReport& report) noexcept
{
    if (Data->Operating)
    {
        return std::unexpected(MakeHostError(HostError::kBusy));
    }
    if (Data->Value != HostState::kClosing && Data->Value != HostState::kClosed)
    {
        return Data->Reject(report, HostError::kInvalidState);
    }
    const State::Operation operation(*Data);
    auto result = Data->Report(HostOperationStage::kShutdown);
    if (Data->Manager)
    {
        module::ModuleOperationReport moduleReport;
        const auto status = Data->Manager->TryFinishShutdown(moduleReport);
        result.ModuleReport = moduleReport;
        if (!status)
        {
            result.PrimaryError = status.error();
            report = result;
            return status;
        }
        Data->Manager.reset();
    }
    Data->Value = HostState::kClosed;
    result.State = Data->Value;
    report = result;
    return {};
}
Result<module::ScopeHandle> EngineHost::CreateContext(HostOperationReport& report) noexcept
{
    return Data->OperateScope(
        [&](module::ModuleOperationReport& moduleReport)
        { return Data->Manager->CreateScope(service::Scope::kContext, Data->Manager->HostScope(), moduleReport); },
        report);
}
Result<module::ScopeHandle> EngineHost::CreateWorld(module::ScopeHandle context, HostOperationReport& report) noexcept
{
    return Data->OperateScope([&](module::ModuleOperationReport& moduleReport)
                              { return Data->Manager->CreateScope(service::Scope::kWorld, context, moduleReport); },
                              report);
}
Status EngineHost::BeginCloseScope(module::ScopeHandle scope, HostOperationReport& report) noexcept
{
    return Data->OperateScope([&](module::ModuleOperationReport& moduleReport)
                              { return Data->Manager->BeginCloseScope(scope, moduleReport); }, report);
}
Status EngineHost::TryFinishCloseScope(module::ScopeHandle scope, HostOperationReport& report) noexcept
{
    return Data->OperateScope([&](module::ModuleOperationReport& moduleReport)
                              { return Data->Manager->TryFinishCloseScope(scope, moduleReport); }, report);
}
Status EngineHost::PollCleanup(HostOperationReport& report) noexcept
{
    return Data->OperateScope([&](module::ModuleOperationReport& moduleReport)
                              { return Data->Manager->PollCleanup(moduleReport); }, report);
}
} // namespace pg::engine
