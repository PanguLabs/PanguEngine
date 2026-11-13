#pragma once
#include <Error.h>
#include <FailureReport.h>
#include <ModuleDeclaration.h>
#include <ModuleDiagnostics.h>
#include <ModuleHandle.h>
#include <ModuleIdentity.h>
#include <ScopeIdentity.h>
#include <optional>
namespace pg::module
{
enum class ModuleCleanupOwner : std::uint8_t
{
    kNone,
    // 调用方继续协调已交付 Module 的卸载或 Scope 的显式关闭。
    kCallerUnload,
    kManagerCleanup,
    kManagerShutdown,
};
enum class ModuleOperationProgress : std::uint8_t
{
    kUnchanged,
    kLoaded,
    kRegistered,
    kRolledBack,
    kActive,
    kCleanupPending,
    kUnloaded,
    kScopeReady,
    kScopeClosed,
};

// 有界拥有型报告；不保存回调、字符串视图或借用的报告地址。
struct ModuleOperationReport
{
    ModuleOperationProgress Progress{ModuleOperationProgress::kUnchanged};
    std::optional<pg::Error> PrimaryError;
    ModuleId Module{};
    ModuleId RelatedModule{};
    ScopeHandle Scope{};
    DeclarationId Declaration{};
    std::optional<pg::Error> OriginalError = std::nullopt;
    std::optional<pg::Error> CleanupError = std::nullopt;
    ModuleHandle Handle{};
    // 仅表达继续清理的协调责任；Finish 入口由原操作与对应句柄决定。
    ModuleCleanupOwner CleanupOwner{ModuleCleanupOwner::kNone};
    FailureReport Failure{};
    ModuleFailureOrigin FailureOrigin{ModuleFailureOrigin::kUnknown};
    std::array<ModuleWaitReason, kMaxModuleWaitReasons> WaitReasons{};
    std::size_t WaitReasonCount{};
    std::size_t RemainingWaitReasons{};
    bool WaitReasonsTruncated{};
    bool FailureHistoryTruncated{};
};

static_assert(sizeof(ModuleOperationReport) <= 4096);

} // namespace pg::module
