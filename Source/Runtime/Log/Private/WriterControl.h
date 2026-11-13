#pragma once

#include "LogQueue.h"

#include <LogWriter.h>
#include <PanguLogExport.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace pg::log::detail
{
class NativeLogThread;
// Gate 只有 open/busy/closed/closed+busy 四种值，不是无界递增引用计数。
// 单个在途构造保证固定记录预算；生产者竞争可以拒收任意等级。
struct WriterControl
{
    static constexpr std::uint8_t kBusy = 1;
    static constexpr std::uint8_t kClosed = 2;
    std::atomic<std::uint64_t> References{1};
    std::atomic<std::uint8_t> Gate;
    std::array<std::atomic<std::uint64_t>, kLogLevelCount> Contention{};
    std::atomic<bool> CounterOverflow;
    LogQueue* Queue{};
    NativeLogThread* ConsumerThread{};
    Level Minimum{Level::kInfo};
};
struct WriterSnapshot
{
    std::array<std::uint64_t, kLogLevelCount> Contention{};
    // 包括饱和和并发计数损失；最终快照必须保守标记不完整。
    bool CounterOverflow{};
};

// 仅由 LogSystem 的串行管理端拥有；Queue 必须活到 Reset 成功。
// Close 关门，IsQuiescent 确认所有已获保护的提交已返回；Reset 才断开队列。
class PG_LOG_API WriterControlOwner final
{
public:
    WriterControlOwner() noexcept = default;
    ~WriterControlOwner();
    WriterControlOwner(const WriterControlOwner&) = delete;
    WriterControlOwner& operator=(const WriterControlOwner&) = delete;
    WriterControlOwner(WriterControlOwner&&) = delete;
    WriterControlOwner& operator=(WriterControlOwner&&) = delete;

    [[nodiscard]] bool Initialize(LogQueue& queue, Level minimum, NativeLogThread* consumerThread = nullptr) noexcept;
    [[nodiscard]] LogWriter GetWriter(LogTags tags = {}) const noexcept;
    void Close() noexcept;
    [[nodiscard]] bool IsQuiescent() const noexcept;
    [[nodiscard]] bool Reset() noexcept;
    [[nodiscard]] WriterSnapshot Inspect() const noexcept;
    [[nodiscard]] static constexpr std::size_t ControlBytes() noexcept { return sizeof(WriterControl); }
    [[nodiscard]] static constexpr std::size_t InFlightRecordBytes() noexcept { return sizeof(LogRecord); }

private:
    friend struct WriterTestAccess;
    friend struct LogTestAccess;
    WriterControl* Control{};
};
} // namespace pg::log::detail
