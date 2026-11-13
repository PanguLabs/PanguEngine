#pragma once

#include <LogReports.h>

namespace pg::log::detail
{
// 受实例 SnapshotLock 保护；超时不移除 Active，Stop 最多保留一个旧前缀。
struct LogDrain
{
    std::optional<FlushReport> Active;
    bool RetainForStop{};
    bool StopTargetFixed{};
};
[[nodiscard]] FlushReport MakeFlushReport(const LogSnapshot& snapshot, bool finished) noexcept;
void CompleteFlush(FlushReport& report, const LogSnapshot& processed, std::uint64_t encodingTruncated,
                   bool encodingOverflow) noexcept;
void CaptureCleanup(FlushReport& report, const LogSnapshot& snapshot) noexcept;
void AddLogCount(std::uint64_t& value, std::uint64_t addition, bool& overflow) noexcept;
} // namespace pg::log::detail
