#include "LogQueue.h"

#include "LogMemory.h"

#include <LogRecord.h>
#include <LogTypes.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <thread>
#include <utility>

namespace pg::log::detail
{
namespace
{
// 临界区只复制固定记录和数值；不调用生产者代码或 I/O。
class UnlockOnExit final
{
public:
    explicit UnlockOnExit(std::atomic_flag& lock) noexcept
        : Lock(lock)
    {
    }
    ~UnlockOnExit() { Lock.clear(std::memory_order_release); }
    UnlockOnExit(const UnlockOnExit&) = delete;
    UnlockOnExit& operator=(const UnlockOnExit&) = delete;
    UnlockOnExit(UnlockOnExit&&) = delete;
    UnlockOnExit& operator=(UnlockOnExit&&) = delete;

private:
    std::atomic_flag& Lock;
};
void Increment(std::uint64_t& counter, std::atomic<bool>& overflow) noexcept
{
    if (counter == std::numeric_limits<std::uint64_t>::max())
    {
        overflow.store(true, std::memory_order_relaxed);
    }
    else
    {
        ++counter;
    }
}
} // namespace

void CountLogEvent(std::atomic<std::uint64_t>& counter, std::atomic<bool>& overflow) noexcept
{
    auto value = counter.load(std::memory_order_relaxed);
    if (value == std::numeric_limits<std::uint64_t>::max() ||
        !counter.compare_exchange_strong(value, value + 1, std::memory_order_relaxed))
    {
        // 包括饱和及并发采样损失；任何不可信统计都禁止完整性成功。
        overflow.store(true, std::memory_order_relaxed);
    }
}

LogQueue::~LogQueue()
{
    if (OwnsStorage)
    {
        std::ranges::destroy(Records);
        FreeLogMemory(Records.data());
    }
}

bool LogQueue::Initialize(std::size_t capacity, std::size_t priorityReserve) noexcept
{
    if (IsInitialized() || capacity == 0 || capacity > 4096 || priorityReserve > 512 || priorityReserve >= capacity ||
        capacity > std::numeric_limits<std::size_t>::max() / sizeof(LogRecord))
    {
        return false;
    }
    static_assert(alignof(LogRecord) <= alignof(std::max_align_t));
    auto* memory = static_cast<LogRecord*>(TryAllocateLogMemory(capacity * sizeof(LogRecord)));
    if (memory == nullptr)
    {
        return false;
    }
    Records = {memory, capacity};
    OwnsStorage = true;
    for (auto& record : Records)
    {
        std::construct_at(&record);
    }
    PriorityReserve = priorityReserve;
    return true;
}

bool LogQueue::Initialize(std::span<LogRecord> storage, std::size_t priorityReserve) noexcept
{
    if (IsInitialized() || storage.empty() || storage.size() > 4096 || priorityReserve > 512 ||
        priorityReserve >= storage.size())
    {
        return false;
    }
    Records = storage;
    PriorityReserve = priorityReserve;
    return true;
}

SubmitResult LogQueue::TryPush(LogRecord&& record) noexcept
{
    const auto level = static_cast<std::size_t>(record.Severity);
    if (level >= kLogLevelCount || Records.empty())
    {
        return SubmitResult::kDisabled;
    }
    auto& rejected = Rejected.at(level);
    if (Lock.test_and_set(std::memory_order_acquire))
    {
        CountLogEvent(rejected.Contention, CounterOverflow);
        return SubmitResult::kDropped;
    }
    const UnlockOnExit unlock{Lock};
    if (HighestAccepted == std::numeric_limits<std::uint64_t>::max())
    {
        SequenceExhausted = true;
        CountLogEvent(rejected.SequenceExhausted, CounterOverflow);
        return SubmitResult::kDropped;
    }
    const auto limit = record.Severity < Level::kWarning ? Records.size() - PriorityReserve : Records.size();
    if (Count >= limit)
    {
        CountLogEvent(rejected.Capacity, CounterOverflow);
        return SubmitResult::kDropped;
    }
    record.Sequence = ++HighestAccepted;
    SequenceExhausted = HighestAccepted == std::numeric_limits<std::uint64_t>::max();
    if (record.Truncated)
    {
        Increment(Truncated, CounterOverflow);
    }
    if (record.ConstructionFailed)
    {
        Increment(ConstructionFailed, CounterOverflow);
    }
    // rvalue 接纳契约允许消费输入，当前记录本身只有固定值。
    // NOLINTNEXTLINE(performance-move-const-arg)
    Records.subspan((Head + Count) % Records.size(), 1).front() = std::move(record);
    ++Count;
    return SubmitResult::kAccepted;
}

bool LogQueue::TryPop(LogRecord& record, std::atomic<bool>* block, std::atomic<bool>* entered) noexcept
{
    if (Lock.test_and_set(std::memory_order_acquire))
    {
        return false;
    }
    const UnlockOnExit unlock{Lock};
    // 仅 Cold 接入的实例测试传入屏障，确定复现空轮询持锁；默认路径不等待。
    if (block != nullptr && entered != nullptr && block->load(std::memory_order_acquire))
    {
        entered->store(true, std::memory_order_release);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (block->load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
    }
    if (Count == 0)
    {
        return false;
    }
    record = Records.subspan(Head, 1).front();
    Head = (Head + 1) % Records.size();
    --Count;
    return true;
}

QueueSnapshot LogQueue::Inspect() const noexcept
{
    for (;;)
    {
        if (auto snapshot = TryInspect())
        {
            return *snapshot;
        }
    }
}
std::optional<QueueSnapshot> LogQueue::TryInspect() const noexcept
{
    if (Lock.test_and_set(std::memory_order_acquire))
    {
        return std::nullopt;
    }
    const UnlockOnExit unlock{Lock};
    QueueSnapshot snapshot{
        .HighestAccepted = HighestAccepted,
        .Pending = Count,
        .Truncated = Truncated,
        .ConstructionFailed = ConstructionFailed,
        .SequenceExhausted = SequenceExhausted,
    };
    for (std::size_t index = 0; index < kLogLevelCount; ++index)
    {
        const auto& counters = Rejected.at(index);
        snapshot.Rejected.at(index) = {
            .Capacity = counters.Capacity.load(std::memory_order_relaxed),
            .Contention = counters.Contention.load(std::memory_order_relaxed),
            .SequenceExhausted = counters.SequenceExhausted.load(std::memory_order_relaxed),
        };
    }
    snapshot.CounterOverflow = CounterOverflow.load(std::memory_order_relaxed);
    return snapshot;
}
} // namespace pg::log::detail
