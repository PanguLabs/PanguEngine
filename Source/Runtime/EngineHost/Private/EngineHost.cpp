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
#include <Check.h>
#include <LogReports.h>
#include <LogSystem.h>
#include <LogTypes.h>
#include <LogWriter.h>
#include <ModuleDiagnostics.h>
#include <chrono>

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
    PG_CHECK(!Data->Operating, "Host 操作尚未返回时不能销毁拥有者");
    HostOperationReport report;
    const State::Operation operation(*Data);
    // Module 只推进一轮；Log 的有限等待不能被 GetState/Inspect 的无界锁提前消耗。
    const auto now = std::chrono::steady_clock::now();
    const auto available =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::time_point::max() - now);
    const auto deadline =
        Data->DefaultWait >= available ? std::chrono::steady_clock::time_point::max() : now + Data->DefaultWait;
    module::ModuleOperationReport moduleReport;
    PG_CHECK(Data->FinishShutdown(report, moduleReport, deadline, HostOperationStage::kShutdown, {}, false, true),
             "Host 在有限等待后仍未安全关闭");
}
module::ModuleManager* detail::EngineHostTestAccess::Manager(EngineHost& host) noexcept
{
    return host.Data->Manager ? &*host.Data->Manager : nullptr;
}
log::LogSystem& detail::EngineHostTestAccess::Log(EngineHost& host) noexcept { return host.Data->Log; }
HostState EngineHost::GetState() const noexcept { return Data->Value; }
log::LogWriter EngineHost::GetLogWriter(log::LogTags tags) const noexcept { return Data->Log.GetWriter(tags); }
log::LogSnapshot EngineHost::InspectLog() const noexcept { return Data->Log.Inspect(); }
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
                                      module::ModuleOperationReport* moduleReport) noexcept
{
    auto& startup = StartupFailure.emplace(
        HostStartupFailure{.Stage = stage, .Cause = cause, .ModuleReport = std::nullopt, .Origin = std::nullopt});
    if (moduleReport != nullptr)
    {
        startup.ModuleReport = HostStartupModuleSummary{
            .Progress = moduleReport->Progress,
            .PrimaryError = moduleReport->PrimaryError,
            .Module = moduleReport->Module,
            .RelatedModule = moduleReport->RelatedModule,
            .Scope = moduleReport->Scope,
            .Declaration = moduleReport->Declaration,
            .OriginalError = moduleReport->OriginalError,
            .CleanupError = moduleReport->CleanupError,
            .Handle = moduleReport->Handle,
            .CleanupOwner = moduleReport->CleanupOwner,
            .FailureOrigin = moduleReport->FailureOrigin,
            .FailureHistoryTruncated = moduleReport->FailureHistoryTruncated,
            .FailureTruncated = moduleReport->Failure.Truncated(),
            .WaitReasonCount = moduleReport->WaitReasonCount,
            .RemainingWaitReasons = moduleReport->RemainingWaitReasons,
            .WaitReasonsTruncated = moduleReport->WaitReasonsTruncated,
            .DetailsOmitted = moduleReport->Failure.EntryCount() != 0 || moduleReport->WaitReasonCount != 0 ||
                              moduleReport->RemainingWaitReasons != 0 || moduleReport->Failure.Truncated()};
        if (moduleReport->FailureOrigin != module::ModuleFailureOrigin::kUnknown)
        {
            const auto origin = moduleReport->Failure.PrimarySite();
            if (origin && origin->Cause == cause)
            {
                startup.Origin = origin;
            }
        }
    }
    Value = HostState::kClosing;
    if (moduleReport != nullptr)
    {
        // 首错摘要已拥有；复用原 Module 报告区推进回滚，避免再持一份完整报告。
        const auto cleanup =
            FinishShutdown(report, *moduleReport, std::chrono::steady_clock::now(), stage, cause, true, true);
        // 返回仍是 E1；清理状态/错误由本次报告交付。
        static_cast<void>(cleanup.has_value());
    }
    else
    {
        module::ModuleOperationReport cleanup;
        const auto stopped =
            FinishShutdown(report, cleanup, std::chrono::steady_clock::now(), stage, cause, false, true);
        static_cast<void>(stopped.has_value());
    }
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
                return FailStartup(report, stage, snapshot.error(), nullptr);
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
                return FailStartup(report, stage, result.error(), &moduleReport);
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
    {
        log::LogInitReport initialization;
        const auto initialized = Data->Log.Initialize(config.Logging, initialization);
        Data->LogInitialization = {.Disposition = initialization.Disposition,
                                   .Enabled = initialization.Enabled,
                                   .Ready = initialization.Ready,
                                   .Failed = initialization.Failed,
                                   .Failure = initialization.Failure,
                                   .Outputs = initialization.Outputs,
                                   .CleanupPending = initialization.CleanupPending};
        Data->LogValue = initialization.CleanupPending ? log::LogState::kClosing : log::LogState::kClosed;
        if (initialized && initialization.Disposition != log::LogInitDisposition::kDisabled)
        {
            Data->LogValue = log::LogState::kRunning;
        }
        // 配置检查失败时沿用有限默认值，不接受负等待时间。
        if (config.Logging.DefaultWait.count() >= 0)
        {
            Data->DefaultWait = config.Logging.DefaultWait;
        }
        if (!initialized)
        {
            return Data->FailStartup(report, HostOperationStage::kLogInitialization, initialized.error(), nullptr);
        }
    }
    const auto builtins = module::BuiltinResidentQueries();
    const auto residentCount = builtins.size() + 1;
    constexpr auto kMaxBindings = module::kMaxCatalogModules * module::kMaxModuleDependencies;
    if (config.AdditionalResidentQueries.size() > module::kMaxCatalogModules - residentCount ||
        config.ResidentBindings.size() > kMaxBindings)
    {
        return Data->FailStartup(report, HostOperationStage::kResidentAdmission,
                                 module::MakeModuleError(module::ModuleError::kLimitExceeded), nullptr);
    }
    std::vector<module::QueryModuleFn> residents(builtins.begin(), builtins.end());
    residents.push_back(QueryEngineHostModule);
    residents.insert(residents.end(), config.AdditionalResidentQueries.begin(), config.AdditionalResidentQueries.end());
    {
        module::ModuleOperationReport admission;
        auto manager = module::ModuleManager::Create(Data->Declarations, residents, config.ResidentBindings, admission,
                                                     Data->Log.GetWriter());
        if (!manager)
        {
            return Data->FailStartup(report, HostOperationStage::kResidentAdmission, manager.error(), &admission);
        }
        Data->Manager.emplace(std::move(*manager));
    }
    auto& activeManager = *Data->Manager;
    const auto residentStarted = Data->StartBatch(activeManager, true, report);
    if (!residentStarted)
    {
        return residentStarted;
    }
    if (config.Modules.size() > module::kMaxCatalogModules - residents.size() ||
        config.ModuleBindings.size() > kMaxBindings)
    {
        return Data->FailStartup(report, HostOperationStage::kBusinessAdmission,
                                 module::MakeModuleError(module::ModuleError::kLimitExceeded), nullptr);
    }
    if (!config.Modules.empty())
    {
        {
            module::ModuleOperationReport admission;
            const auto loaded = activeManager.Load(config.Modules, config.ModuleBindings, admission);
            if (!loaded)
            {
                return Data->FailStartup(report, HostOperationStage::kBusinessAdmission, loaded.error(), &admission);
            }
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
    module::ModuleOperationReport moduleReport;
    if (!Data->Manager)
    {
        return Data->FinishShutdown(report, moduleReport, std::chrono::steady_clock::now());
    }
    Data->Value = HostState::kClosing;
    const auto status = Data->Manager->BeginShutdown(moduleReport);
    // 嵌套 Manager 的报告工作区先退出，再组装完整 Host 报告。
    auto result = Data->Report(HostOperationStage::kShutdown);
    result.ModuleReport = moduleReport;
    if (!status)
    {
        result.PrimaryError = status.error();
    }
    report = result;
    return status;
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
    module::ModuleOperationReport moduleReport;
    return Data->FinishShutdown(report, moduleReport, std::chrono::steady_clock::now());
}
Status EngineHost::State::FinishShutdown(HostOperationReport& report, module::ModuleOperationReport& moduleReport,
                                         std::chrono::steady_clock::time_point deadline, HostOperationStage stage,
                                         std::optional<pg::Error> startupCause, bool hasModuleReport,
                                         bool begin) noexcept
{
    hasModuleReport = Manager.has_value() || hasModuleReport;
    Status status;
    if (Manager)
    {
        Value = HostState::kClosing;
        if (begin)
        {
            status = Manager->BeginShutdown(moduleReport);
        }
        if (status)
        {
            status = Manager->TryFinishShutdown(moduleReport);
        }
        if (status)
        {
            Manager.reset();
        }
    }
    // 完整 Host 报告及 LogStop 只在嵌套 Module 操作返回之后同时存活。
    auto result = Report(stage);
    if (hasModuleReport)
    {
        result.ModuleReport = moduleReport;
    }
    if (!Manager)
    {
        auto& stopped = result.LogStop.emplace();
        status = Log.Stop(deadline, stopped);
        LogValue = stopped.ResourcesReleased ? log::LogState::kClosed : log::LogState::kClosing;
        Value = stopped.ResourcesReleased ? HostState::kClosed : HostState::kClosing;
        result.WaitingForLog = !stopped.ResourcesReleased;
    }
    result.LogState = LogValue;
    result.State = Value;
    result.PrimaryError = startupCause;
    if (!startupCause && !status)
    {
        result.PrimaryError = status.error();
    }
    report = result;
    return status;
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
