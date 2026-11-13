#include "WriterControl.h"

#include "LogMemory.h"
#include "LogQueue.h"
#include "NativeLogThread.h"

#include <Check.h>
#include <LogRecord.h>
#include <LogTypes.h>
#include <LogWriter.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

namespace pg::log
{
namespace
{
bool Retain(detail::WriterControl* control) noexcept
{
    if (control == nullptr)
    {
        return true;
    }
    auto count = control->References.load(std::memory_order_relaxed);
    while (count != std::numeric_limits<std::uint64_t>::max())
    {
        if (control->References.compare_exchange_weak(count, count + 1, std::memory_order_relaxed))
        {
            return true;
        }
    }
    control->CounterOverflow.store(true, std::memory_order_relaxed);
    return false;
}
void Release(detail::WriterControl* control) noexcept
{
    if (control != nullptr && control->References.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        std::destroy_at(control);
        detail::FreeLogMemory(control);
    }
}
class SubmissionGuard final
{
public:
    explicit SubmissionGuard(detail::WriterControl& control) noexcept
        : Control(control)
    {
    }
    ~SubmissionGuard() { Control.Gate.fetch_and(detail::WriterControl::kClosed, std::memory_order_release); }
    SubmissionGuard(const SubmissionGuard&) = delete;
    SubmissionGuard& operator=(const SubmissionGuard&) = delete;
    SubmissionGuard(SubmissionGuard&&) = delete;
    SubmissionGuard& operator=(SubmissionGuard&&) = delete;

private:
    detail::WriterControl& Control;
};
} // namespace

LogWriter::LogWriter(detail::WriterControl* control, LogTags tags) noexcept
    : Control(control)
    , Tags(tags)
{
    if (!Retain(Control))
    {
        Control = nullptr;
    }
}
LogWriter::~LogWriter() { Release(Control); }
LogWriter::LogWriter(const LogWriter& other) noexcept
    : LogWriter(other.Control, other.Tags)
{
}
LogWriter& LogWriter::operator=(const LogWriter& other) noexcept
{
    if (this != &other)
    {
        auto* replacement = Retain(other.Control) ? other.Control : nullptr;
        Release(Control);
        Control = replacement;
        Tags = other.Tags;
    }
    return *this;
}
LogWriter::LogWriter(LogWriter&& other) noexcept
    : Control(std::exchange(other.Control, nullptr))
    , Tags(other.Tags)
{
}
LogWriter& LogWriter::operator=(LogWriter&& other) noexcept
{
    if (this != &other)
    {
        Release(Control);
        Control = std::exchange(other.Control, nullptr);
        Tags = other.Tags;
    }
    return *this;
}
LogWriter LogWriter::WithTags(LogTags tags) const noexcept { return LogWriter{Control, tags}; }

SubmitResult LogWriter::SubmitBorrowed(Level level, const void* context,
                                       void (*build)(const void*, LogRecord&) noexcept) const noexcept
{
    if (Control == nullptr)
    {
        return SubmitResult::kDisabled;
    }
    if ((Control->Gate.load(std::memory_order_acquire) & detail::WriterControl::kClosed) != 0)
    {
        return SubmitResult::kClosed;
    }
    if (level >= Level::kOff || level < Control->Minimum)
    {
        return SubmitResult::kFiltered;
    }
    std::uint8_t expected{};
    if (!Control->Gate.compare_exchange_strong(expected, detail::WriterControl::kBusy, std::memory_order_acquire))
    {
        if ((expected & detail::WriterControl::kClosed) != 0)
        {
            return SubmitResult::kClosed;
        }
        detail::CountLogEvent(Control->Contention.at(static_cast<std::size_t>(level)), Control->CounterOverflow);
        return SubmitResult::kDropped;
    }
    const SubmissionGuard guard{*Control};
    // 原生消费者身份只在提交保护内读取，避免与关闸后的线程回收竞争。
    if (Control->ConsumerThread != nullptr && Control->ConsumerThread->IsCurrent())
    {
        detail::CountLogEvent(Control->Contention.at(static_cast<std::size_t>(level)), Control->CounterOverflow);
        return SubmitResult::kDropped;
    }
    LogRecord record;
    build(context, record);
    // 标签属于句柄，不能由上下文默认值或构造回调清空。
    record.Tags = Tags;
    record.Severity = level;
    // 队列的接纳接口明确消费 rvalue，固定值记录不含动态资源。
    // NOLINTNEXTLINE(performance-move-const-arg)
    return Control->Queue->TryPush(std::move(record));
}

namespace detail
{
WriterControlOwner::~WriterControlOwner()
{
    Close();
    PG_CHECK(Reset(), "Log Writer owner destroyed with an in-flight submission");
}
bool WriterControlOwner::Initialize(LogQueue& queue, Level minimum, NativeLogThread* consumerThread) noexcept
{
    if (Control != nullptr || !queue.IsInitialized() || minimum > Level::kOff)
    {
        return false;
    }
    static_assert(alignof(WriterControl) <= alignof(std::max_align_t));
    auto* memory = static_cast<WriterControl*>(TryAllocateLogMemory(sizeof(WriterControl)));
    if (memory == nullptr)
    {
        return false;
    }
    Control = std::construct_at(memory);
    Control->Queue = &queue;
    Control->ConsumerThread = consumerThread;
    Control->Minimum = minimum;
    return true;
}
LogWriter WriterControlOwner::GetWriter(LogTags tags) const noexcept { return LogWriter{Control, tags}; }
void WriterControlOwner::Close() noexcept
{
    if (Control != nullptr)
    {
        Control->Gate.fetch_or(WriterControl::kClosed, std::memory_order_acq_rel);
    }
}
bool WriterControlOwner::IsQuiescent() const noexcept
{
    return Control == nullptr || Control->Gate.load(std::memory_order_acquire) == WriterControl::kClosed;
}
bool WriterControlOwner::Reset() noexcept
{
    if (!IsQuiescent())
    {
        return false;
    }
    if (Control != nullptr)
    {
        Control->Queue = nullptr;
        Control->ConsumerThread = nullptr;
        Release(std::exchange(Control, nullptr));
    }
    return true;
}
WriterSnapshot WriterControlOwner::Inspect() const noexcept
{
    WriterSnapshot snapshot;
    if (Control != nullptr)
    {
        for (std::size_t index = 0; index < kLogLevelCount; ++index)
        {
            snapshot.Contention.at(index) = Control->Contention.at(index).load(std::memory_order_relaxed);
        }
        snapshot.CounterOverflow = Control->CounterOverflow.load(std::memory_order_relaxed);
    }
    return snapshot;
}
} // namespace detail
} // namespace pg::log
