#pragma once

#include <FailureContext.h>
#include <LogWriter.h>
#include <ModuleOperation.h>

namespace pg::module::detail
{
struct ModuleHistoryDiagnostic
{
    ModuleHandle ModuleOwner{};
    ScopeHandle ScopeOwner{};
    ModuleId Module{};
    ScopeHandle Scope{};
    DeclarationId Declaration{};
    FailureReport Failure;
    ModuleFailureOrigin Origin{ModuleFailureOrigin::kUnknown};
    bool Used{};
};
struct ModulePollDiagnostic
{
    std::uint64_t Serial{};
    std::optional<FailureSite> Site;
};
inline constexpr std::size_t kModuleDiagnosticReportBytes = sizeof(std::array<ModuleHistoryDiagnostic, 8>) +
                                                            sizeof(std::array<ModulePollDiagnostic, 8>) +
                                                            (2 * sizeof(FailureReport));

inline void AppendFailure(FailureReport& destination, const FailureReport& source) noexcept
{
    FailureContext context(destination);
    if (const auto site = source.PrimarySite())
    {
        // 跨报告没有足够的阶段/对象身份供去重；相同 Error 也可能来自不同现场。
        context.AddSecondary(*site);
    }
    for (std::size_t index = 0; index < source.EntryCount(); ++index)
    {
        if (const auto site = source.SecondarySite(index))
        {
            context.AddSecondary(*site);
        }
        else
        {
            context.AddContext(source.ContextKey(index), source.ContextValue(index));
        }
    }
    // 保留源报告已截断的显式提示；归并仍受目标的固定容量限制。
    if (source.Truncated())
    {
        context.AddContext("source.truncated", "true");
    }
}

inline void MatchFailure(FailureReport& failure, Error error, ModuleFailureOrigin& origin) noexcept
{
    const auto primary = failure.PrimarySite();
    if (primary && primary->Cause == error)
    {
        if (!primary->OriginKnown())
        {
            origin = ModuleFailureOrigin::kUnknown;
        }
        return;
    }
    FailureReport matched;
    FailureContext context(matched);
    context.SetPrimary(error, "callback returned error without matching origin", {});
    AppendFailure(matched, failure);
    failure = matched;
    origin = ModuleFailureOrigin::kUnknown;
}

// 外部回调可能复用调用方的目标报告；只在操作栈退出时发布局部快照。
class ReportPublication final
{
public:
    ReportPublication(ModuleOperationReport& destination, ModuleOperationReport& local) noexcept
        : Destination(&destination)
        , Local(&local)
    {
    }
    ~ReportPublication()
    {
        if (Local->PrimaryError)
        {
            MatchFailure(Local->Failure, *Local->PrimaryError, Local->FailureOrigin);
        }
        *Destination = *Local;
    }
    ReportPublication(const ReportPublication&) = delete;
    ReportPublication& operator=(const ReportPublication&) = delete;
    ReportPublication(ReportPublication&&) = delete;
    ReportPublication& operator=(ReportPublication&&) = delete;

private:
    ModuleOperationReport* Destination;
    ModuleOperationReport* Local;
};

inline void AddWait(ModuleOperationReport& report, ModuleWaitReason reason) noexcept
{
    if (report.WaitReasonCount < report.WaitReasons.size())
    {
        report.WaitReasons.at(report.WaitReasonCount++) = reason;
    }
    else
    {
        ++report.RemainingWaitReasons;
        report.WaitReasonsTruncated = true;
    }
}

[[nodiscard]] inline log::LogWriter TaggedWriter(const log::LogWriter& writer, ModuleId module, ScopeHandle scope = {},
                                                 DeclarationId declaration = {}) noexcept
{
    log::LogTags tags{};
    tags.Module = module;
    if (scope.IsValid())
    {
        tags.Scope = log::LogScopeTag{.Domain = scope.Domain, .Slot = scope.Slot, .Generation = scope.Generation};
    }
    if (declaration.IsValid())
    {
        tags.Declaration = declaration.Bytes;
    }
    return writer.WithTags(tags);
}
} // namespace pg::module::detail
