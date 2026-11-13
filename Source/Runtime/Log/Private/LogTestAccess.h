#pragma once

#include "NativeLogIo.h"
#include <LogSystem.h>

#include <atomic>

namespace pg::log::detail
{
enum class LogInitStage : std::uint8_t
{
    kNone,
    kConfigCopy,
    kDataAllocation,
    kQueue,
    kControlAllocation,
    kSynchronization,
    kConsole,
    kFile,
    kBackend,
    kThread,
};

// 仅正式测试在 Cold 时接入，借用期覆盖实例；故障字段只在对应资源线程静止时修改。
struct LogTestHooks
{
    LogInitStage FailAt{LogInitStage::kNone};
    NativeIoFaults Console;
    NativeIoFaults File;
    std::atomic<bool> BlockOutput;
    std::atomic<bool> OutputEntered;
    std::atomic<bool> BlockQueuePop;
    std::atomic<bool> QueuePopEntered;
    std::atomic<std::uint64_t> BlockSequence;
    std::atomic<bool> SequenceEntered;
    std::atomic<bool> ProbeReentry;
    std::atomic<SubmitResult> ReentrySubmit;
    std::atomic<bool> ReentryFlushBusy;
    std::atomic<bool> ReentryStopBusy;
    std::atomic<bool> BlockExit;
    std::atomic<bool> ExitEntered;
    std::atomic<std::size_t> DataBlocks;
    std::atomic<std::size_t> ControlOwners;
    std::atomic<std::size_t> WorkerHandles;
    std::atomic<std::size_t> CleanupAttempts;
};

struct LogMemoryUsage
{
    std::size_t ObjectBytes{};
    std::size_t DataBytes{};
    std::size_t ControlBytes{};
    std::size_t SubmissionWorkBytes{};
    std::size_t WorkerStackBytes{};
    std::size_t ManagementWorkBytes{};
    std::size_t RequiredBytes{};
};

struct PG_LOG_API LogTestAccess
{
    static void Attach(LogSystem& system, LogTestHooks& hooks) noexcept { system.Hooks = &hooks; }
    [[nodiscard]] static LogMemoryUsage MemoryUsage(const LogConfig& config) noexcept;
    static void MarkCountersUntrusted(LogSystem& system) noexcept;
    static void SeedRejectionSum(LogSystem& system) noexcept;
    static void HoldQueue(LogSystem& system) noexcept;
    static void ReleaseQueue(LogSystem& system) noexcept;
    [[nodiscard]] static bool TryHoldSnapshot(LogSystem& system) noexcept
    {
        return !system.SnapshotLock.test_and_set(std::memory_order_acquire);
    }
    static void ReleaseSnapshot(LogSystem& system) noexcept { system.SnapshotLock.clear(std::memory_order_release); }
};
} // namespace pg::log::detail
