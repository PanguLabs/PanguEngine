#pragma once

#include <LogReports.h>
#include <LogTypes.h>
#include <cstddef>

namespace pg::fixture
{
// 单次 Info 提交的结果必须与拥有型停止报告一致，不能把累计拒收写成完整交付。
[[nodiscard]] inline bool ValidateLogOutcome(log::SubmitResult submitted, const log::StopReport& stopped) noexcept
{
    const bool accepted = submitted == log::SubmitResult::kAccepted;
    if (!accepted && submitted != log::SubmitResult::kDropped)
    {
        return false;
    }
    const auto& final = stopped.FinalFlush;
    const auto outputs = log::OutputMask::kConsole | log::OutputMask::kFile;
    const auto delivered = accepted ? 1U : 0U;
    if (!stopped.ResourcesReleased || !final.Finished || final.H != delivered || final.OutputComplete != accepted ||
        final.Enabled != outputs || final.Ready != outputs || final.Failed != log::OutputMask::kNone ||
        final.InitializationFailure || final.CounterOverflow || final.SequenceExhausted || final.Truncated != 0 ||
        final.ConstructionFailed != 0)
    {
        return false;
    }
    for (std::size_t index = 0; index < final.Rejected.size(); ++index)
    {
        const auto expected = !accepted && index == static_cast<std::size_t>(log::Level::kInfo) ? 1U : 0U;
        const auto& rejected = final.Rejected.at(index);
        if (rejected.Capacity != 0 || rejected.Contention != expected || rejected.SequenceExhausted != 0)
        {
            return false;
        }
    }
    for (std::size_t index = 0; index < final.Outputs.size(); ++index)
    {
        const auto& output = final.Outputs.at(index);
        const auto& control = final.ControlOutputs.at(index);
        if (output.Delivered != delivered || output.Undelivered != 0 || !output.FlushFinished ||
            !output.FlushSucceeded || output.Failure || output.CleanupFailure || control.Failure ||
            control.Undelivered != 0)
        {
            return false;
        }
    }
    return true;
}
} // namespace pg::fixture
