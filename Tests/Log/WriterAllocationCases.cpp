#include "WriterAllocationCases.h"

#include <Log.h>
#include <LogQueue.h>
#include <LogRecord.h>
#include <LogTypes.h>
#include <LogWriter.h>
#include <WriterControl.h>

#include <array>
#include <cstdint>
#include <utility>

namespace
{
struct WriterAllocationState
{
    std::array<pg::log::LogRecord, 4> Slots;
    pg::log::detail::LogQueue Queue;
    pg::log::detail::WriterControlOwner Owner;
    pg::log::LogWriter Writer;
};
WriterAllocationState& State()
{
    static WriterAllocationState state;
    return state;
}
} // namespace

bool PrepareWriterAllocationCases() noexcept
{
    auto& state = State();
    if (!state.Queue.Initialize(state.Slots, 1) || !state.Owner.Initialize(state.Queue, pg::log::Level::kInfo))
    {
        return false;
    }
    state.Writer = state.Owner.GetWriter();
    return true;
}

bool RunWriterAllocationCases() noexcept
{
    using pg::log::SubmitResult;
    auto& state = State();
    auto copy = state.Writer;
    const auto moved = std::move(copy);
    pg::log::LogTags tags;
    tags.Operation = 42;
    const auto tagged = moved.WithTags(tags);
    bool success = PG_LOG_ERROR(tagged, "allocation", "{} {} {}", 42, 1.25, "text") == SubmitResult::kAccepted;
    success = (PG_LOG_DEBUG(tagged, "filtered", "unused") == SubmitResult::kFiltered) && success;
    for (int index = 0; index < 3; ++index)
    {
        success = (PG_LOG_ERROR(tagged, "capacity", "{}", index) == SubmitResult::kAccepted) && success;
    }
    success = (PG_LOG_ERROR(tagged, "full", "drop") == SubmitResult::kDropped) && success;
    state.Owner.Close();
    success = state.Owner.IsQuiescent() && success;
    success = state.Owner.Reset() && success;
    state.Writer = {};
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) 关闭后的复制也必须无分配。
    const auto stale = tagged;
    success = (PG_LOG_ERROR(stale, "closed", "drop") == SubmitResult::kClosed) && success;
    pg::log::LogRecord record;
    std::uint64_t count{};
    while (state.Queue.TryPop(record))
    {
        success = record.Sequence == ++count && success;
    }
    return success && count == 4;
}

bool RunWriterOutOfMemoryCases() noexcept
{
    // 调用方已令实际 malloc 接缝返回 nullptr；分别覆盖 slot 和独立控制块。
    pg::log::detail::LogQueue queue;
    if (queue.Initialize(4, 1) || queue.IsInitialized())
    {
        return false;
    }
    std::array<pg::log::LogRecord, 4> slots;
    if (!queue.Initialize(slots, 1))
    {
        return false;
    }
    pg::log::detail::WriterControlOwner owner;
    return !owner.Initialize(queue, pg::log::Level::kInfo) &&
           PG_LOG_ERROR(owner.GetWriter(), "oom", "disabled") == pg::log::SubmitResult::kDisabled;
}
