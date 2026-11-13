#pragma once

#include <LogRecord.h>
#include <PanguLogExport.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace pg::log::detail
{
inline constexpr std::size_t kLogLevelCount = 5;
struct RejectionCounts
{
    std::uint64_t Capacity{};
    std::uint64_t Contention{};
    std::uint64_t SequenceExhausted{};
};
struct QueueSnapshot
{
    std::uint64_t HighestAccepted{};
    std::size_t Pending{};
    std::array<RejectionCounts, kLogLevelCount> Rejected{};
    std::uint64_t Truncated{};
    std::uint64_t ConstructionFailed{};
    // 饱和或并发计数 CAS 失败均表示统计不可信；必须使 OutputComplete 为 false。
    bool CounterOverflow{};
    bool SequenceExhausted{};
};

// 竞争计数也只尝试一次 CAS；竞争导致统计丢失时置不可信标志，不能误报输出完整。
PG_LOG_API void CountLogEvent(std::atomic<std::uint64_t>& counter, std::atomic<bool>& overflow) noexcept;

struct AtomicRejectionCounts
{
    std::atomic<std::uint64_t> Capacity;
    std::atomic<std::uint64_t> Contention;
    std::atomic<std::uint64_t> SequenceExhausted;
};

// Owner 在关闭 Writer 且等待在途提交结束后才能销毁；一个消费者，管理操作由 Owner 串行。
class PG_LOG_API LogQueue final
{
public:
    LogQueue() noexcept = default;
    ~LogQueue();
    LogQueue(const LogQueue&) = delete;
    LogQueue& operator=(const LogQueue&) = delete;
    LogQueue(LogQueue&&) = delete;
    LogQueue& operator=(LogQueue&&) = delete;

    [[nodiscard]] bool Initialize(std::size_t capacity, std::size_t priorityReserve) noexcept;
    // 可借用实例单块数据存储内已构造的记录；不销毁或释放该 span，寿命由实例管理。
    [[nodiscard]] bool Initialize(std::span<LogRecord> storage, std::size_t priorityReserve) noexcept;
    SubmitResult TryPush(LogRecord&& record) noexcept;
    [[nodiscard]] bool TryPop(LogRecord& record, std::atomic<bool>* block = nullptr,
                              std::atomic<bool>* entered = nullptr) noexcept;
    // 管理端可等短临界区；生产者和消费者只尝试一次，不在此等待。
    [[nodiscard]] QueueSnapshot Inspect() const noexcept;
    [[nodiscard]] std::optional<QueueSnapshot> TryInspect() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept { return !Records.empty(); }
    [[nodiscard]] static constexpr std::size_t SlotBytes() noexcept { return sizeof(LogRecord); }

private:
    friend struct WriterTestAccess;
    friend struct LogTestAccess;
    std::span<LogRecord> Records;
    bool OwnsStorage{};
    std::size_t PriorityReserve{};
    std::size_t Head{};
    std::size_t Count{};
    std::uint64_t HighestAccepted{};
    std::uint64_t Truncated{};
    std::uint64_t ConstructionFailed{};
    bool SequenceExhausted{};
    mutable std::atomic_flag Lock;
    std::array<AtomicRejectionCounts, kLogLevelCount> Rejected{};
    std::atomic<bool> CounterOverflow;
};
} // namespace pg::log::detail
