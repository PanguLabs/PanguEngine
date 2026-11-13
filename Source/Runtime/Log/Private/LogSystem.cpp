#include "Error.h"
#include "LogConfig.h"
#include "LogDrain.h"
#include "LogMemory.h"
#include "LogQueue.h"
#include "LogRecord.h"
#include "LogReports.h"
#include "LogSystemState.h"
#include "LogTestAccess.h"
#include "LogTypes.h"
#include "LogWriter.h"
#include "NativeLogIo.h"
#include "NativeLogThread.h"
#include "Result.h"
#include "WriterControl.h"
#include <Check.h>
#include <Log.h>
#include <LogFormat.h>
#include <LogSystem.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <fmt/base.h>
#include <limits>
#include <memory>
#include <span>
#include <thread>

namespace pg::log
{
namespace
{
class FlagGuard final
{
public:
    explicit FlagGuard(
        std::atomic_flag& flag, bool wait = true,
        std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max()) noexcept
        : Flag(flag)
    {
        for (;;)
        {
            Acquired = !Flag.test_and_set(std::memory_order_acquire);
            if (Acquired || !wait || std::chrono::steady_clock::now() >= deadline)
            {
                return;
            }
            std::this_thread::yield();
        }
    }
    ~FlagGuard()
    {
        if (Acquired)
        {
            Flag.clear(std::memory_order_release);
        }
    }
    FlagGuard(const FlagGuard&) = delete;
    FlagGuard& operator=(const FlagGuard&) = delete;
    FlagGuard(FlagGuard&&) = delete;
    FlagGuard& operator=(FlagGuard&&) = delete;
    [[nodiscard]] bool Owns() const noexcept { return Acquired; }

private:
    std::atomic_flag& Flag;
    bool Acquired{};
};
constexpr std::array kOutputs{OutputMask::kConsole, OutputMask::kFile};
constexpr auto kSlotOffset =
    ((sizeof(detail::LogSystemData) + alignof(LogRecord) - 1) / alignof(LogRecord)) * alignof(LogRecord);
// 归一化临时值、tuple、消息和 fmt 固定迭代缓冲与一份在途记录同时计入预算。
constexpr auto kSubmissionWork = sizeof(LogRecord) +
                                 (2 * sizeof(std::array<detail::LogIdentityText, detail::kMaxLogArguments>)) +
                                 kLogMessageCapacity + sizeof(detail::LogFormatBuffer) +
                                 sizeof(fmt::detail::iterator_buffer<detail::LogOutputIterator, char>);
// 一次管理调用的报告/快照与一份查询工作副本；消费者栈工作由请求栈预算覆盖。
constexpr auto kManagementWork =
    (2 * sizeof(LogSnapshot)) + sizeof(LogInitReport) + sizeof(StopReport) + sizeof(FlushReport);
constexpr LogMemoryLayout kMemoryLayout{
    .QueueSlotBytes = sizeof(LogRecord),
    .FixedBytes = sizeof(LogSystem) + kSlotOffset + detail::WriterControlOwner::ControlBytes() + kSubmissionWork +
                  kManagementWork,
};
static_assert(alignof(detail::LogSystemData) <= alignof(std::max_align_t));
bool Inject(const detail::LogTestHooks* hooks, detail::LogInitStage stage) noexcept
{
    return hooks != nullptr && hooks->FailAt == stage;
}
using detail::MakeFlushReport;
void FillInitReport(const LogSnapshot& snapshot, LogInitReport& report) noexcept
{
    report.Enabled = snapshot.Enabled;
    report.Ready = snapshot.Ready;
    report.Failed = snapshot.Failed;
    report.Failure = snapshot.InitializationFailure;
    report.Outputs = snapshot.Outputs;
    report.File = snapshot.File;
    report.FilePath = snapshot.File.ActivePath;
}
std::chrono::steady_clock::time_point DeadlineAfter(std::chrono::milliseconds wait) noexcept
{
    const auto now = std::chrono::steady_clock::now();
    const auto available =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::time_point::max() - now);
    return wait >= available ? std::chrono::steady_clock::time_point::max() : now + wait;
}
} // namespace
detail::LogMemoryUsage detail::LogTestAccess::MemoryUsage(const LogConfig& config) noexcept
{
    const auto required = RequiredLogMemory(config, kMemoryLayout);
    return {
        .ObjectBytes = sizeof(LogSystem),
        .DataBytes = kSlotOffset + (config.QueueCapacity * sizeof(LogRecord)),
        .ControlBytes = WriterControlOwner::ControlBytes(),
        .SubmissionWorkBytes = kSubmissionWork,
        .WorkerStackBytes = config.WorkerStackBytes,
        .ManagementWorkBytes = kManagementWork,
        .RequiredBytes = required.value_or(0),
    };
}
LogSystem::~LogSystem()
{
    StopReport report;
    const auto result = Stop(DeadlineAfter(DefaultWait), report);
    PG_CHECK(result && report.ResourcesReleased, "日志实例在有限等待后仍未安全停止");
}
void detail::LogTestAccess::MarkCountersUntrusted(LogSystem& system) noexcept
{
    system.Data->EncodingOverflow.store(true);
}
void detail::LogTestAccess::SeedRejectionSum(LogSystem& system) noexcept
{
    // 仅消费者尚无记录、生产者静止时使用；两路各自未溢出，合并必须检测溢出。
    system.Data->Queue.Rejected.at(2).Contention.store(std::numeric_limits<std::uint64_t>::max());
    system.Data->Writer.Control->Contention.at(2).store(1);
}
void detail::LogTestAccess::HoldQueue(LogSystem& system) noexcept
{
    while (system.Data->Queue.Lock.test_and_set(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }
}
void detail::LogTestAccess::ReleaseQueue(LogSystem& system) noexcept
{
    system.Data->Queue.Lock.clear(std::memory_order_release);
}
LogState LogSystem::GetState() const noexcept
{
    const FlagGuard guard{SnapshotLock};
    return Snapshot.State;
}
LogWriter LogSystem::GetWriter(LogTags tags) const noexcept
{
    const FlagGuard guard{SnapshotLock};
    return Snapshot.State == LogState::kRunning && Data != nullptr ? Data->Writer.GetWriter(tags) : LogWriter{};
}
LogSnapshot LogSystem::Inspect() const noexcept
{
    const FlagGuard guard{SnapshotLock};
    auto snapshot = Snapshot;
    if (Snapshot.State != LogState::kCold && Data != nullptr)
    {
        Data->CaptureProducers(snapshot);
    }
    return snapshot;
}
void LogSystem::Publish() noexcept
{
    const FlagGuard guard{SnapshotLock};
    const auto state = Snapshot.State;
    Snapshot = Data->Working;
    if (state == LogState::kClosing)
    {
        Snapshot.State = LogState::kClosing;
    }
    if (Data->Finalized)
    {
        // 只推进停止的清理账目，不重新采集生产者或覆盖旧 Flush 的前缀。
        detail::CaptureCleanup(FinalStop.FinalFlush, Snapshot);
    }
}
void LogSystem::ReleaseData() noexcept
{
    // join、quiescent 和两路无残留资源之后才进入；此处不执行持资源的 I/O。
    PG_CHECK(Data != nullptr && !Data->Worker.OwnsResource(), "日志数据释放前必须回收原生线程");
    Data->Writer.Close();
    PG_CHECK(Data->Writer.Reset(), "日志数据释放前必须结束在途提交");
    if (Data->ControlOwned && Hooks != nullptr)
    {
        Hooks->ControlOwners.fetch_sub(1);
    }
    const auto records = Data->Records;
    auto* const memory = Data;
    std::destroy_at(Data);
    for (auto& record : records)
    {
        std::destroy_at(&record);
    }
    detail::FreeLogMemory(memory);
    Data = nullptr;
    if (Hooks != nullptr)
    {
        Hooks->DataBlocks.fetch_sub(1);
    }
}
void LogSystem::FinishWithoutWorker() noexcept
{
    // 调用方已持有 SnapshotLock，且原生线程不再访问实例。
    if (Data != nullptr)
    {
        Data->CaptureProducers(Data->Working);
        Snapshot = Data->Working;
        ReleaseData();
    }
    Snapshot.State = LogState::kClosed;
    if (!FinalStop.FinalFlush.Finished)
    {
        FinalStop.FinalFlush = MakeFlushReport(Snapshot, true);
    }
    // Close 的责任与错误在最终回收点交付，不改写已保存的旧 Flush。
    detail::CaptureCleanup(FinalStop.FinalFlush, Snapshot);
    FinalStop.ResourcesReleased = true;
}
Status LogSystem::Rollback(Error error, LogInitReport& report) noexcept
{
    report.Disposition = LogInitDisposition::kFailed;
    {
        const FlagGuard guard{SnapshotLock};
        Snapshot.InitializationFailure = error;
        Snapshot.State = LogState::kClosing;
    }
    if (Data == nullptr)
    {
        const FlagGuard guard{SnapshotLock};
        FinishWithoutWorker();
    }
    else
    {
        Data->Working.InitializationFailure = error;
        Data->Working.State = LogState::kClosing;
        Data->Writer.Close();
        Data->CleanupOnly = true;
        bool released{};
        if (Data->Worker.OwnsResource())
        {
            Data->StopRequested.store(true, std::memory_order_release);
            Data->Published.store(true, std::memory_order_release);
            const auto joined = Data->Worker.TryJoin(DeadlineAfter(DefaultWait));
            released = joined.has_value();
            if (released && Hooks != nullptr)
            {
                Hooks->WorkerHandles.fetch_sub(1);
            }
        }
        else
        {
            released = Data->CloseOutputs();
            Publish();
        }
        if (released)
        {
            const FlagGuard guard{SnapshotLock};
            FinishWithoutWorker();
        }
    }
    {
        const FlagGuard guard{SnapshotLock};
        FillInitReport(Snapshot, report);
        report.CleanupPending = Data != nullptr;
    }
    return std::unexpected(error);
}
Status LogSystem::Initialize(const LogConfig& config, LogInitReport& report) noexcept
{
    const FlagGuard management{ManagementLock, false};
    if (!management.Owns())
    {
        return std::unexpected(MakeLogError(LogError::kBusy));
    }
    if (GetState() != LogState::kCold)
    {
        return std::unexpected(MakeLogError(LogError::kInvalidState));
    }
    report = {};
    {
        const FlagGuard guard{SnapshotLock};
        Snapshot.Enabled = config.Enabled;
    }
    const auto valid = ValidateLogConfig(config, kMemoryLayout);
    if (!valid || config.WorkerStackBytes < detail::kMinimumLogWorkerStackBytes)
    {
        return Rollback(!valid ? valid.error() : MakeLogError(LogError::kInvalidConfig), report);
    }
    DefaultWait = config.DefaultWait;
    if (config.Enabled == OutputMask::kNone)
    {
        report.Disposition = LogInitDisposition::kDisabled;
        const FlagGuard guard{SnapshotLock};
        FinishWithoutWorker();
        FillInitReport(Snapshot, report);
        return {};
    }
    if (Inject(Hooks, detail::LogInitStage::kConfigCopy) || Inject(Hooks, detail::LogInitStage::kDataAllocation))
    {
        return Rollback(MakeLogError(LogError::kOutOfMemory), report);
    }
    const auto dataBytes = kSlotOffset + (config.QueueCapacity * sizeof(LogRecord));
    void* const memory = detail::TryAllocateLogMemory(dataBytes);
    if (memory == nullptr)
    {
        return Rollback(MakeLogError(LogError::kOutOfMemory), report);
    }
    Data = std::construct_at(static_cast<detail::LogSystemData*>(memory), *this, Hooks);
    if (Hooks != nullptr)
    {
        Hooks->DataBlocks.fetch_add(1);
    }
    Data->Config = config;
    const auto fileValid = ValidateLogFileConfig(config);
    // 超限路径不截断成另一条路径，也不保存调用者的借用视图。
    if (config.Directory.size() <= kLogPathCapacity)
    {
        Data->Directory.Size = config.Directory.size();
        std::ranges::copy(config.Directory, Data->Directory.Bytes.begin());
    }
    Data->Config.Directory = Data->Directory.View();
    void* const recordMemory = std::span(static_cast<std::byte*>(memory), dataBytes).subspan(kSlotOffset).data();
    auto* const slots = static_cast<LogRecord*>(recordMemory);
    Data->Records = {slots, config.QueueCapacity};
    for (auto& record : Data->Records)
    {
        std::construct_at(&record);
    }
    Data->Working = Snapshot;
    if (Inject(Hooks, detail::LogInitStage::kQueue) || !Data->Queue.Initialize(Data->Records, config.PriorityReserve))
    {
        return Rollback(MakeLogError(LogError::kOutOfMemory), report);
    }
    if (Inject(Hooks, detail::LogInitStage::kControlAllocation) ||
        !Data->Writer.Initialize(Data->Queue, config.Minimum, &Data->Worker))
    {
        return Rollback(MakeLogError(LogError::kOutOfMemory), report);
    }
    Data->ControlOwned = true;
    if (Hooks != nullptr)
    {
        Hooks->ControlOwners.fetch_add(1);
    }
    if (Inject(Hooks, detail::LogInitStage::kSynchronization))
    {
        return Rollback(MakeLogError(LogError::kThreadStartFailed), report);
    }
    for (std::size_t index = 0; index < kOutputs.size(); ++index)
    {
        const auto output = kOutputs.at(index);
        if (!HasOutput(config.Enabled, output))
        {
            continue;
        }
        Status opened;
        if (index == 0)
        {
            opened = Inject(Hooks, detail::LogInitStage::kConsole)
                         ? Status{std::unexpected(MakeLogError(LogError::kIoFailed))}
                         : Data->Console.OpenConsole();
        }
        else
        {
            if (!fileValid)
            {
                opened = fileValid;
            }
            else if (Inject(Hooks, detail::LogInitStage::kFile))
            {
                opened = std::unexpected(MakeLogError(LogError::kFileOpenFailed));
            }
            else
            {
                opened = Data->File.Open(Data->Config, report);
            }
            Data->File.Inspect(Data->Working.File);
        }
        if (opened)
        {
            Data->Working.Ready = Data->Working.Ready | output;
        }
        else
        {
            Data->Working.Failed = Data->Working.Failed | output;
            Data->Working.Outputs.at(index).Failure = opened.error();
            if (HasOutput(config.Required, output))
            {
                return Rollback(opened.error(), report);
            }
        }
    }
    if (Data->Working.Ready == OutputMask::kNone)
    {
        return Rollback(MakeLogError(LogError::kNoOutputs), report);
    }
    if (Inject(Hooks, detail::LogInitStage::kBackend))
    {
        return Rollback(MakeLogError(LogError::kOutOfMemory), report);
    }
    Data->Backend.emplace(Data->Port(0), Data->Port(1));
    if (Inject(Hooks, detail::LogInitStage::kThread))
    {
        return Rollback(MakeLogError(LogError::kThreadStartFailed), report);
    }
    const auto started = Data->Worker.Start(detail::LogSystemData::Run, Data, config.WorkerStackBytes);
    if (Data->Worker.OwnsResource() && Hooks != nullptr)
    {
        Hooks->WorkerHandles.fetch_add(1);
    }
    if (!started)
    {
        return Rollback(started.error(), report);
    }
    Data->Working.State = LogState::kRunning;
    Publish();
    report.Disposition =
        Data->Working.Failed == OutputMask::kNone ? LogInitDisposition::kReady : LogInitDisposition::kDegraded;
    FillInitReport(Data->Working, report);
    Data->Published.store(true, std::memory_order_release);
    return {};
}
Status LogSystem::Flush(std::chrono::steady_clock::time_point deadline, FlushReport& report) noexcept
{
    report = {};
    const FlagGuard management{ManagementLock, false};
    if (!management.Owns())
    {
        return std::unexpected(MakeLogError(LogError::kBusy));
    }
    for (;;)
    {
        {
            const FlagGuard guard{SnapshotLock, true, deadline};
            if (!guard.Owns())
            {
                return std::unexpected(MakeLogError(LogError::kBusy));
            }
            if (Data != nullptr && Data->Worker.IsCurrent())
            {
                return std::unexpected(MakeLogError(LogError::kBusy));
            }
            if ((Snapshot.State == LogState::kClosing || Snapshot.State == LogState::kClosed) &&
                FinalStop.PreviousFlush)
            {
                report = *FinalStop.PreviousFlush;
                return {};
            }
            if (Data == nullptr || (Snapshot.State != LogState::kRunning && !Data->Drain.Active))
            {
                return std::unexpected(MakeLogError(LogError::kInvalidState));
            }
            if (!Data->Drain.Active)
            {
                // 与消费者下一次出队互斥；接纳锁内得到 H，后续记录不能越过检查点。
                auto snapshot = Snapshot;
                if (Data->TryCaptureProducers(snapshot))
                {
                    Data->Drain.Active = MakeFlushReport(snapshot, false);
                }
            }
            if (Data->Drain.Active)
            {
                report = *Data->Drain.Active;
                if (report.Finished)
                {
                    if (Snapshot.State == LogState::kRunning)
                    {
                        Data->Drain.Active.reset();
                    }
                    return {};
                }
            }
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return std::unexpected(MakeLogError(LogError::kBusy));
        }
        std::this_thread::yield();
    }
}
Status LogSystem::Stop(std::chrono::steady_clock::time_point deadline, StopReport& report) noexcept
{
    report = {};
    const FlagGuard management{ManagementLock, false};
    if (!management.Owns())
    {
        return std::unexpected(MakeLogError(LogError::kBusy));
    }
    {
        const FlagGuard guard{SnapshotLock, true, deadline};
        if (!guard.Owns())
        {
            return std::unexpected(MakeLogError(LogError::kBusy));
        }
        if (Snapshot.State == LogState::kClosed)
        {
            report = FinalStop;
            return {};
        }
        if (Data != nullptr && Data->Worker.IsCurrent())
        {
            return std::unexpected(MakeLogError(LogError::kBusy));
        }
        Snapshot.State = LogState::kClosing;
        if (Data != nullptr)
        {
            Data->Writer.Close();
            Data->Drain.RetainForStop = Data->Drain.Active.has_value();
            if (Data->Drain.Active && Data->Drain.Active->Finished)
            {
                // 旧检查点已完成时即刻交接；不能依赖正在后续 I/O 中的消费者再推进。
                FinalStop.PreviousFlush = *Data->Drain.Active;
                Data->Drain.Active.reset();
            }
            Data->StopRequested.store(true, std::memory_order_release);
            Data->RetryCleanup.store(true, std::memory_order_release);
        }
    }
    if (Data == nullptr)
    {
        const FlagGuard guard{SnapshotLock, true, deadline};
        if (!guard.Owns())
        {
            return std::unexpected(MakeLogError(LogError::kBusy));
        }
        FinishWithoutWorker();
        report = FinalStop;
        return {};
    }
    if (!Data->NativeReaped && !Data->Worker.OwnsResource())
    {
        // 回滚残留同样交给原生线程，Stop 调用线程不执行可能阻塞的 Close。
        Data->CleanupOnly = true;
        const auto started = Data->Worker.Start(detail::LogSystemData::Run, Data, Data->Config.WorkerStackBytes);
        if (Data->Worker.OwnsResource() && Hooks != nullptr)
        {
            Hooks->WorkerHandles.fetch_add(1);
        }
        if (!started)
        {
            const FlagGuard guard{SnapshotLock, false};
            if (guard.Owns())
            {
                report = FinalStop;
            }
            return started;
        }
        Data->Published.store(true, std::memory_order_release);
    }
    if (!Data->NativeReaped)
    {
        const auto joined = Data->Worker.TryJoin(deadline);
        if (!joined)
        {
            const FlagGuard guard{SnapshotLock, false};
            if (guard.Owns())
            {
                report = FinalStop;
            }
            return joined;
        }
        Data->NativeReaped = true;
        if (Hooks != nullptr)
        {
            Hooks->WorkerHandles.fetch_sub(1);
        }
    }
    const FlagGuard guard{SnapshotLock, true, deadline};
    if (!guard.Owns())
    {
        return std::unexpected(MakeLogError(LogError::kBusy));
    }
    FinishWithoutWorker();
    report = FinalStop;
    return {};
}
namespace detail
{
void LogSystemData::CaptureProducers(LogSnapshot& snapshot) const noexcept
{
    MergeProducers(snapshot, Queue.Inspect());
    AddLogCount(snapshot.Truncated, EncodingTruncated.load(std::memory_order_relaxed), snapshot.CounterOverflow);
    snapshot.CounterOverflow = snapshot.CounterOverflow || EncodingOverflow.load(std::memory_order_relaxed);
}
bool LogSystemData::TryCaptureProducers(LogSnapshot& snapshot) const noexcept
{
    const auto queue = Queue.TryInspect();
    if (!queue)
    {
        return false;
    }
    MergeProducers(snapshot, *queue);
    return true;
}
void LogSystemData::MergeProducers(LogSnapshot& snapshot, const QueueSnapshot& queue) const noexcept
{
    const auto writer = Writer.Inspect();
    snapshot.HighestAccepted = queue.HighestAccepted;
    snapshot.Truncated = queue.Truncated;
    snapshot.ConstructionFailed = queue.ConstructionFailed;
    snapshot.CounterOverflow = snapshot.CounterOverflow || queue.CounterOverflow || writer.CounterOverflow;
    snapshot.SequenceExhausted = queue.SequenceExhausted;
    for (std::size_t index = 0; index < kLogLevelCount; ++index)
    {
        const auto& counts = queue.Rejected.at(index);
        auto& target = snapshot.Rejected.at(index);
        target = {
            .Capacity = counts.Capacity,
            .Contention = counts.Contention,
            .SequenceExhausted = counts.SequenceExhausted,
        };
        AddLogCount(target.Contention, writer.Contention.at(index), snapshot.CounterOverflow);
    }
}
OutputOutcome LogSystemData::WriteOutput(std::size_t index, std::string_view text) noexcept
{
    const auto& output = Working.Outputs.at(index);
    if (!HasOutput(Working.Ready, kOutputs.at(index)))
    {
        return {.Failure = output.Failure};
    }
    if (Hooks != nullptr)
    {
        if (Hooks->ProbeReentry.exchange(false))
        {
            Hooks->ReentrySubmit.store(PG_LOG_INFO(Owner.GetWriter(), "reentry", "forbidden"));
            FlushReport flush;
            const auto flushed = Owner.Flush(std::chrono::steady_clock::now(), flush);
            Hooks->ReentryFlushBusy.store(!flushed && flushed.error() == MakeLogError(LogError::kBusy));
            StopReport stop;
            const auto stopped = Owner.Stop(std::chrono::steady_clock::now(), stop);
            Hooks->ReentryStopBusy.store(!stopped && stopped.error() == MakeLogError(LogError::kBusy));
        }
        Hooks->OutputEntered.store(true, std::memory_order_release);
        while (Hooks->BlockOutput.load(std::memory_order_acquire))
        {
            PauseLogWorker();
        }
    }
    return index == 0 ? Console.Write(text) : File.Write(text);
}
OutputOutcome LogSystemData::FlushOutput(std::size_t index) noexcept
{
    if (!HasOutput(Working.Ready, kOutputs.at(index)))
    {
        return {.Failure = Working.Outputs.at(index).Failure};
    }
    return index == 0 ? Console.Flush() : File.Flush();
}
OutputPort LogSystemData::Port(std::size_t index) noexcept
{
    if (index == 0)
    {
        return {
            .Context = this,
            .Write = [](void* context, std::string_view text) noexcept
            { return static_cast<LogSystemData*>(context)->WriteOutput(0, text); },
            .Flush = [](void* context) noexcept { return static_cast<LogSystemData*>(context)->FlushOutput(0); },
        };
    }
    return {
        .Context = this,
        .Write = [](void* context, std::string_view text) noexcept
        { return static_cast<LogSystemData*>(context)->WriteOutput(1, text); },
        .Flush = [](void* context) noexcept { return static_cast<LogSystemData*>(context)->FlushOutput(1); },
    };
}
void LogSystemData::RecordOutcomes(const std::array<OutputOutcome, 2>& outcomes, bool flush) noexcept
{
    for (std::size_t index = 0; index < kOutputs.size(); ++index)
    {
        const auto mask = kOutputs.at(index);
        if (!HasOutput(Working.Enabled, mask))
        {
            continue;
        }
        const auto& outcome = outcomes.at(index);
        auto& output = Working.Outputs.at(index);
        if (flush)
        {
            output.FlushFinished = true;
            output.FlushSucceeded = outcome.Complete;
        }
        else
        {
            AddLogCount(outcome.Complete ? output.Delivered : output.Undelivered, 1, Working.CounterOverflow);
            AddLogCount(output.BytesWritten, outcome.BytesWritten, Working.CounterOverflow);
        }
        if (!outcome.Complete)
        {
            if (!output.Failure)
            {
                output.Failure = outcome.Failure.value_or(MakeLogError(LogError::kIoFailed));
            }
            Working.Failed = Working.Failed | mask;
            Working.Ready =
                static_cast<OutputMask>(static_cast<unsigned int>(Working.Ready) & ~static_cast<unsigned int>(mask));
        }
    }
    File.Inspect(Working.File);
    Working.CounterOverflow = Working.CounterOverflow || Working.File.CounterOverflow;
}
bool LogSystemData::CloseOutputs() noexcept
{
    const auto file = File.Close();
    File.Inspect(Working.File);
    if (!file)
    {
        Working.Outputs.at(1).CleanupFailure = file.error();
    }
    const auto console = Console.Close();
    if (!console)
    {
        Working.Outputs.at(0).CleanupFailure = console.error();
    }
    if (Hooks != nullptr)
    {
        Hooks->CleanupAttempts.fetch_add(1, std::memory_order_release);
    }
    return !Working.File.CleanupPending && !Console.OwnsResource();
}
void LogSystemData::EmitControlNotice() noexcept
{
    if (Working.ControlNoticeAttempted || Working.Failed == OutputMask::kNone)
    {
        return;
    }
    Working.ControlNoticeAttempted = true;
    std::array<char, 1024> text{};
    constexpr std::string_view kPrefix = "log degraded";
    std::ranges::copy(kPrefix, text.begin());
    std::size_t used = kPrefix.size();
    const auto append = [&](std::string_view label, std::uint64_t value) noexcept
    {
        // 固定字段最多约 700 B；仍给所有文本和整数保留显式边界。
        if (label.size() + 21 >= text.size() - used)
        {
            return;
        }
        std::ranges::copy(label, std::span{text}.subspan(used).begin());
        used += label.size();
        const auto converted =
            std::to_chars(std::span{text}.subspan(used).data(), std::span{text}.last(1).data(), value);
        used = static_cast<std::size_t>(converted.ptr - text.data());
    };
    append(" outputs=", static_cast<unsigned int>(Working.Failed));
    append(" processed=", Working.HighestProcessed);
    for (const auto& output : Working.Outputs)
    {
        const auto error = output.Failure.value_or(MakeLogError(LogError::kIoFailed));
        append(" domain=", output.Failure ? error.Domain().Value() : 0);
        append(" code=", output.Failure ? error.Code() : 0);
        append(" delivered=", output.Delivered);
        append(" undelivered=", output.Undelivered);
    }
    // 控制文本独立采样生产者窗口；不得把 H 之后的统计不可信写回 Working。
    if (const auto queue = Queue.TryInspect())
    {
        const auto writer = Writer.Inspect();
        bool overflow = Working.CounterOverflow || queue->CounterOverflow || writer.CounterOverflow;
        for (std::size_t index = 0; index < kLogLevelCount; ++index)
        {
            const auto& rejected = queue->Rejected.at(index);
            auto contention = rejected.Contention;
            AddLogCount(contention, writer.Contention.at(index), overflow);
            append(" capacity=", rejected.Capacity);
            append(" contention=", contention);
            append(" exhausted=", rejected.SequenceExhausted);
        }
        append(" truncated=", queue->Truncated);
        append(" construction_failed=", queue->ConstructionFailed);
        append(" counter_untrusted=", overflow ? 1 : 0);
    }
    text.at(used++) = '\n';
    const std::string_view notice{text.data(), used};
    for (std::size_t index = 0; index < kOutputs.size(); ++index)
    {
        if (!HasOutput(Working.Ready, kOutputs.at(index)))
        {
            continue;
        }
        const auto outcome = WriteOutput(index, notice);
        auto& control = Working.ControlOutputs.at(index);
        AddLogCount(outcome.Complete ? control.Delivered : control.Undelivered, 1, Working.CounterOverflow);
        AddLogCount(control.BytesWritten, outcome.BytesWritten, Working.CounterOverflow);
        if (!outcome.Complete)
        {
            control.Failure = outcome.Failure.value_or(MakeLogError(LogError::kIoFailed));
            Working.Outputs.at(index).Failure = control.Failure;
            Working.Failed = Working.Failed | kOutputs.at(index);
            Working.Ready = static_cast<OutputMask>(static_cast<unsigned int>(Working.Ready) &
                                                    ~static_cast<unsigned int>(kOutputs.at(index)));
        }
    }
    File.Inspect(Working.File);
}
void LogSystemData::Run(void* context) noexcept { static_cast<LogSystemData*>(context)->Consume(); }
void LogSystemData::Consume() noexcept
{
    while (!Published.load(std::memory_order_acquire))
    {
        PauseLogWorker();
    }
    for (;;)
    {
        bool checkpoint{};
        bool finalCheckpoint{};
        bool popped{};
        {
            // 与建立请求使用同一短锁；绝不持锁执行普通 I/O。
            const FlagGuard guard{Owner.SnapshotLock};
            if (StopRequested.load(std::memory_order_acquire) && !Drain.StopTargetFixed && Writer.IsQuiescent())
            {
                auto snapshot = Owner.Snapshot;
                if (TryCaptureProducers(snapshot))
                {
                    Owner.FinalStop.FinalFlush = MakeFlushReport(snapshot, false);
                    Drain.StopTargetFixed = true;
                    Working.State = LogState::kClosing;
                }
            }
            if (Drain.Active && Drain.Active->Finished && Drain.RetainForStop)
            {
                Owner.FinalStop.PreviousFlush = *Drain.Active;
                Drain.Active.reset();
            }
            checkpoint = Drain.Active && !Drain.Active->Finished && Working.HighestProcessed == Drain.Active->H;
            finalCheckpoint = !checkpoint && Drain.StopTargetFixed && !Drain.Active && !Finalized &&
                              Working.HighestProcessed == Owner.FinalStop.FinalFlush.H;
            if (!checkpoint && !finalCheckpoint && !Finalized && !CleanupOnly)
            {
                // 已完成但未领取的请求允许继续处理；其拥有型结果保持原前缀。
                popped = Queue.TryPop(Record, Hooks != nullptr ? &Hooks->BlockQueuePop : nullptr,
                                      Hooks != nullptr ? &Hooks->QueuePopEntered : nullptr);
            }
        }
        if (checkpoint || finalCheckpoint)
        {
            EmitControlNotice();
            if (Backend && !CleanupOnly)
            {
                RecordOutcomes(Backend->Flush(), true);
                EmitControlNotice();
            }
            {
                const FlagGuard guard{Owner.SnapshotLock};
                auto& target = checkpoint ? *Drain.Active : Owner.FinalStop.FinalFlush;
                CompleteFlush(target, Working, EncodingTruncated.load(std::memory_order_relaxed),
                              EncodingOverflow.load(std::memory_order_relaxed));
                if (checkpoint && Drain.RetainForStop)
                {
                    Owner.FinalStop.PreviousFlush = target;
                    Drain.Active.reset();
                }
                if (finalCheckpoint)
                {
                    Finalized = true;
                    RetryCleanup.store(true, std::memory_order_release);
                }
            }
            Owner.Publish();
            continue;
        }
        if (Finalized && RetryCleanup.exchange(false, std::memory_order_acq_rel))
        {
            const bool released = CloseOutputs();
            Owner.Publish();
            if (released)
            {
                break;
            }
        }
        else if (popped)
        {
            if (Hooks != nullptr && Hooks->BlockSequence.load(std::memory_order_acquire) == Record.Sequence)
            {
                Hooks->SequenceEntered.store(true, std::memory_order_release);
                while (Hooks->BlockSequence.load(std::memory_order_acquire) != 0)
                {
                    PauseLogWorker();
                }
            }
            EncodeRecord(Record, Line);
            if (Line.Truncated)
            {
                CountLogEvent(EncodingTruncated, EncodingOverflow);
            }
            RecordOutcomes(Backend->Emit(Record.Severity, Line.Text()), false);
            Working.HighestProcessed = Record.Sequence;
            EmitControlNotice();
            Owner.Publish();
            continue;
        }
        PauseLogWorker();
    }
    if (Hooks != nullptr)
    {
        Hooks->ExitEntered.store(true, std::memory_order_release);
        while (Hooks->BlockExit.load(std::memory_order_acquire))
        {
            PauseLogWorker();
        }
    }
}
} // namespace detail
} // namespace pg::log
