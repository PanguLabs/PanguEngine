#pragma once

#include <EngineHost.h>
#include <HostError.h>
#include <LogSystem.h>
#include <optional>
#include <utility>

namespace pg::engine
{
struct EngineHost::State
{
    // 反向析构：Manager 先释放，Log 及其原生依赖随后，声明域最后销毁。
    module::RegistrationScope Declarations;
    log::LogSystem Log;
    std::optional<module::ModuleManager> Manager;
    HostState Value{HostState::kCold};
    bool Operating{};
    std::optional<HostStartupFailure> StartupFailure;
    HostLogInitialization LogInitialization;
    log::LogState LogValue{log::LogState::kCold};
    std::chrono::milliseconds DefaultWait{100};

    struct Operation
    {
        State& Owner;
        explicit Operation(State& owner) noexcept
            : Owner(owner)
        {
            Owner.Operating = true;
        }
        ~Operation() { Owner.Operating = false; }
        Operation(const Operation&) = delete;
        Operation& operator=(const Operation&) = delete;
        Operation(Operation&&) = delete;
        Operation& operator=(Operation&&) = delete;
    };

    [[nodiscard]] HostOperationReport Report(HostOperationStage stage) const noexcept
    {
        return {
            .State = Value,
            .Stage = stage,
            .PrimaryError = std::nullopt,
            .ModuleReport = std::nullopt,
            .StartupFailure = StartupFailure,
            .LogState = LogValue,
            .LogInitialization = LogInitialization,
            .LogStop = std::nullopt,
            .WaitingForLog = false,
        };
    }
    [[nodiscard]] Status Reject(HostOperationReport& report, HostError error) const noexcept
    {
        auto result = Report(HostOperationStage::kNone);
        result.PrimaryError = MakeHostError(error);
        report = result;
        return std::unexpected(MakeHostError(error));
    }
    template <class F>
    [[nodiscard]] auto OperateScope(F&& execute, HostOperationReport& report) noexcept
        -> decltype(execute(std::declval<module::ModuleOperationReport&>()))
    {
        if (Operating)
        {
            return std::unexpected(MakeHostError(HostError::kBusy));
        }
        if (Value != HostState::kReady)
        {
            return std::unexpected(Reject(report, HostError::kInvalidState).error());
        }
        const Operation operation(*this);
        module::ModuleOperationReport moduleReport;
        auto status = std::forward<F>(execute)(moduleReport);
        if (moduleReport.CleanupOwner == module::ModuleCleanupOwner::kManagerShutdown)
        {
            Value = HostState::kClosing;
        }
        auto result = Report(HostOperationStage::kScopeOperation);
        result.ModuleReport = moduleReport;
        if (!status)
        {
            result.PrimaryError = status.error();
        }
        report = result;
        return status;
    }
    [[nodiscard]] Status FailStartup(HostOperationReport& report, HostOperationStage stage, pg::Error cause,
                                     module::ModuleOperationReport* moduleReport) noexcept;
    [[nodiscard]] Status FinishShutdown(HostOperationReport& report, module::ModuleOperationReport& moduleReport,
                                        std::chrono::steady_clock::time_point deadline,
                                        HostOperationStage stage = HostOperationStage::kShutdown,
                                        std::optional<pg::Error> startupCause = {}, bool hasModuleReport = false,
                                        bool begin = false) noexcept;
    [[nodiscard]] Status StartBatch(module::ModuleManager& manager, bool resident,
                                    HostOperationReport& report) noexcept;
};
} // namespace pg::engine
