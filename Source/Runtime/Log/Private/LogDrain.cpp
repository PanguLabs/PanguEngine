#include "LogDrain.h"

#include <LogReports.h>
#include <LogTypes.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace pg::log::detail
{
namespace
{
void UpdateComplete(FlushReport& report) noexcept
{
    report.OutputComplete = report.Finished && !report.InitializationFailure && report.Failed == OutputMask::kNone &&
                            report.Truncated == 0 && report.ConstructionFailed == 0 && !report.CounterOverflow &&
                            !report.SequenceExhausted;
    for (const auto& rejected : report.Rejected)
    {
        report.OutputComplete = report.OutputComplete && rejected.Capacity == 0 && rejected.Contention == 0 &&
                                rejected.SequenceExhausted == 0;
    }
    constexpr std::array kOutputs{OutputMask::kConsole, OutputMask::kFile};
    for (std::size_t index = 0; index < kOutputs.size(); ++index)
    {
        if (HasOutput(report.Enabled, kOutputs.at(index)))
        {
            const auto& output = report.Outputs.at(index);
            report.OutputComplete = report.OutputComplete && !output.Failure && !output.CleanupFailure &&
                                    output.Undelivered == 0 && output.FlushFinished && output.FlushSucceeded;
        }
    }
}
} // namespace
void AddLogCount(std::uint64_t& value, std::uint64_t addition, bool& overflow) noexcept
{
    if (addition > std::numeric_limits<std::uint64_t>::max() - value)
    {
        value = std::numeric_limits<std::uint64_t>::max();
        overflow = true;
    }
    else
    {
        value += addition;
    }
}
FlushReport MakeFlushReport(const LogSnapshot& snapshot, bool finished) noexcept
{
    FlushReport report;
    report.H = snapshot.HighestAccepted;
    report.Finished = finished;
    report.Enabled = snapshot.Enabled;
    report.Ready = snapshot.Ready;
    report.Failed = snapshot.Failed;
    report.InitializationFailure = snapshot.InitializationFailure;
    report.Outputs = snapshot.Outputs;
    report.Rejected = snapshot.Rejected;
    report.Truncated = snapshot.Truncated;
    report.ConstructionFailed = snapshot.ConstructionFailed;
    report.CounterOverflow = snapshot.CounterOverflow;
    report.SequenceExhausted = snapshot.SequenceExhausted;
    report.ControlNoticeAttempted = snapshot.ControlNoticeAttempted;
    report.ControlOutputs = snapshot.ControlOutputs;
    UpdateComplete(report);
    return report;
}
void CompleteFlush(FlushReport& report, const LogSnapshot& processed, std::uint64_t encodingTruncated,
                   bool encodingOverflow) noexcept
{
    report.Finished = true;
    report.Ready = processed.Ready;
    report.Failed = processed.Failed;
    report.Outputs = processed.Outputs;
    report.ControlNoticeAttempted = processed.ControlNoticeAttempted;
    report.ControlOutputs = processed.ControlOutputs;
    report.CounterOverflow = report.CounterOverflow || processed.CounterOverflow || encodingOverflow;
    AddLogCount(report.Truncated, encodingTruncated, report.CounterOverflow);
    UpdateComplete(report);
}
void CaptureCleanup(FlushReport& report, const LogSnapshot& snapshot) noexcept
{
    for (std::size_t index = 0; index < report.Outputs.size(); ++index)
    {
        report.Outputs.at(index).CleanupFailure = snapshot.Outputs.at(index).CleanupFailure;
    }
    report.CounterOverflow = report.CounterOverflow || snapshot.CounterOverflow || snapshot.File.CounterOverflow;
    UpdateComplete(report);
}
} // namespace pg::log::detail
