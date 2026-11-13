#pragma once

#include <FailureReport.h>
#include <LogReports.h>
#include <ModuleOperation.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace pg::engine
{
enum class HostState : std::uint8_t
{
    kCold = 0,
    kStarting = 1,
    kReady = 2,
    kClosing = 3,
    kClosed = 4,
};
enum class HostOperationStage : std::uint8_t
{
    kNone = 0,
    kResidentAdmission = 1,
    kResidentRegistration = 2,
    kResidentActivation = 3,
    kBusinessAdmission = 4,
    kBusinessRegistration = 5,
    kBusinessActivation = 6,
    kScopeOperation = 7,
    kShutdown = 8,
    kLogInitialization = 9,
};
// 历史摘要不是完整 ModuleOperationReport；等待列表和上下文不重复缓存。
struct HostStartupModuleSummary
{
    module::ModuleOperationProgress Progress{};
    std::optional<pg::Error> PrimaryError;
    module::ModuleId Module{};
    module::ModuleId RelatedModule{};
    module::ScopeHandle Scope{};
    module::DeclarationId Declaration{};
    std::optional<pg::Error> OriginalError;
    std::optional<pg::Error> CleanupError;
    module::ModuleHandle Handle{};
    module::ModuleCleanupOwner CleanupOwner{};
    module::ModuleFailureOrigin FailureOrigin{};
    bool FailureHistoryTruncated{};
    bool FailureTruncated{};
    std::size_t WaitReasonCount{};
    std::size_t RemainingWaitReasons{};
    bool WaitReasonsTruncated{};
    bool DetailsOmitted{};
};
struct HostStartupFailure
{
    HostOperationStage Stage;
    pg::Error Cause;
    std::optional<HostStartupModuleSummary> ModuleReport;
    // 只在原始来源已知时保存；传播点不冒充起源。
    std::optional<FailureSite> Origin;
};
// 路径可通过 InspectLog 读取；每次操作只携带固定的初始化能力摘要。
struct HostLogInitialization
{
    log::LogInitDisposition Disposition{log::LogInitDisposition::kUninitialized};
    log::OutputMask Enabled{log::OutputMask::kNone};
    log::OutputMask Ready{log::OutputMask::kNone};
    log::OutputMask Failed{log::OutputMask::kNone};
    std::optional<pg::Error> Failure;
    std::array<log::LogOutputSnapshot, 2> Outputs{};
    bool CleanupPending{};
};
struct HostOperationReport
{
    HostState State{HostState::kCold};
    HostOperationStage Stage{HostOperationStage::kNone};
    std::optional<pg::Error> PrimaryError;
    std::optional<module::ModuleOperationReport> ModuleReport;
    std::optional<HostStartupFailure> StartupFailure;
    log::LogState LogState{log::LogState::kCold};
    HostLogInitialization LogInitialization;
    std::optional<log::StopReport> LogStop;
    bool WaitingForLog{};
    // 当前 Module 现场由本报告拥有；无 Module 报告不等于操作成功。
    // 只在访问时借用，不缓存指向自身的地址，复制后自然指向副本成员。
    [[nodiscard]] const FailureReport* Failure() const noexcept
    {
        return ModuleReport ? &ModuleReport->Failure : nullptr;
    }
};
static_assert(sizeof(HostOperationReport) <= 16384);
} // namespace pg::engine
