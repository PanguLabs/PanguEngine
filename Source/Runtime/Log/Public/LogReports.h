#pragma once

#include <LogConfig.h>

namespace pg::log
{
// 拥有型原生路径；Size 不含终止符，不保存指向自身的视图。
struct LogPath
{
    std::array<std::filesystem::path::value_type, kLogPathCapacity + 1> Bytes{};
    std::size_t Size{};
    [[nodiscard]] NativePathView View() const noexcept { return {Bytes.data(), Size}; }
};

struct FileStoreSnapshot
{
    std::uint64_t ManagedBytes{};
    std::uint64_t ReclaimedBytes{};
    std::uint64_t ReclaimedFiles{};
    bool LeaseHeld{};
    // 表示仍有原生资源或待删除责任；Running 时也可能为 true。
    bool CleanupPending{};
    bool Failed{};
    bool CounterOverflow{};
    std::optional<Error> Failure;
    std::optional<Error> CleanupFailure;
    LogPath ActivePath;
};

enum class LogInitDisposition : std::uint8_t
{
    kUninitialized,
    kReady,
    kDegraded,
    kDisabled,
    kFailed,
};

struct LogRejectionCounts
{
    std::uint64_t Capacity{};
    std::uint64_t Contention{};
    std::uint64_t SequenceExhausted{};
};

struct LogOutputSnapshot
{
    std::optional<Error> Failure;
    std::optional<Error> CleanupFailure;
    std::uint64_t Delivered{};
    std::uint64_t Undelivered{};
    std::uint64_t BytesWritten{};
    bool FlushFinished{};
    bool FlushSucceeded{};
};

// 控制摘要不领取用户序号，也不计入用户记录的送达条数。
struct LogControlSnapshot
{
    std::uint64_t Delivered{};
    std::uint64_t Undelivered{};
    std::uint64_t BytesWritten{};
    std::optional<Error> Failure;
};

struct LogSnapshot
{
    LogState State{LogState::kCold};
    OutputMask Enabled{OutputMask::kNone};
    OutputMask Ready{OutputMask::kNone};
    OutputMask Failed{OutputMask::kNone};
    std::optional<Error> InitializationFailure;
    std::uint64_t HighestAccepted{};
    std::uint64_t HighestProcessed{};
    std::array<LogOutputSnapshot, 2> Outputs{};
    std::array<LogRejectionCounts, 5> Rejected{};
    std::uint64_t Truncated{};
    std::uint64_t ConstructionFailed{};
    bool CounterOverflow{};
    bool SequenceExhausted{};
    bool ControlNoticeAttempted{};
    std::array<LogControlSnapshot, 2> ControlOutputs{};
    FileStoreSnapshot File;
};

struct FlushReport
{
    std::uint64_t H{};
    bool Finished{};
    bool OutputComplete{};
    OutputMask Enabled{OutputMask::kNone};
    OutputMask Ready{OutputMask::kNone};
    OutputMask Failed{OutputMask::kNone};
    std::optional<Error> InitializationFailure;
    std::array<LogOutputSnapshot, 2> Outputs{};
    std::array<LogRejectionCounts, 5> Rejected{};
    std::uint64_t Truncated{};
    std::uint64_t ConstructionFailed{};
    bool CounterOverflow{};
    bool SequenceExhausted{};
    bool ControlNoticeAttempted{};
    std::array<LogControlSnapshot, 2> ControlOutputs{};
};

struct StopReport
{
    std::optional<FlushReport> PreviousFlush;
    FlushReport FinalFlush;
    // 输出完整性见 FinalFlush；此值还要求原生线程已退出并完成回收。
    bool ResourcesReleased{};
};

// FileStore 只更新 File 和 FilePath；实例补齐输出能力与初始化首错。
struct LogInitReport
{
    LogInitDisposition Disposition{LogInitDisposition::kUninitialized};
    LogPath FilePath;
    FileStoreSnapshot File;
    OutputMask Enabled{OutputMask::kNone};
    OutputMask Ready{OutputMask::kNone};
    OutputMask Failed{OutputMask::kNone};
    std::optional<Error> Failure;
    std::array<LogOutputSnapshot, 2> Outputs{};
    // 仅表示初始化失败回滚未完成，不等同于 File.CleanupPending。
    bool CleanupPending{};
};
} // namespace pg::log
