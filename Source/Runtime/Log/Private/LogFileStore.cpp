#include "LogFileStore.h"
#include "Error.h"
#include "LogConfig.h"
#include "LogRecord.h"
#include "LogReports.h"
#include "LogTypes.h"
#include "NativeLogIo.h"
#include "Result.h"
#include <Check.h>
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <ranges>
#include <span>
#include <string_view>
#include <system_error>

namespace pg::log::detail
{
namespace
{
void Hex(std::span<char> output, std::uint64_t value) noexcept
{
    constexpr std::string_view kDigits = "0123456789abcdef";
    for (char& iterator : std::views::reverse(output))
    {
        iterator = kDigits.substr(value & 15U, 1).front();
        value >>= 4U;
    }
}
void SetName(ManagedFile& file) noexcept
{
    file.Name = {};
    std::ranges::copy(std::string_view("pg-"), file.Name.begin());
    Hex(std::span(file.Name).subspan(3, 16), file.Session);
    std::get<19>(file.Name) = '-';
    Hex(std::span(file.Name).subspan(20, 16), file.Segment);
    std::ranges::copy(std::string_view(".log"), std::span(file.Name).subspan(36).begin());
}
void SaturatingAdd(std::uint64_t& target, std::uint64_t value, bool& overflow) noexcept
{
    if (value > std::numeric_limits<std::uint64_t>::max() - target)
    {
        target = std::numeric_limits<std::uint64_t>::max();
        overflow = true;
    }
    else
    {
        target += value;
    }
}
} // namespace
FileHeader MakeFileHeader(const FilePartition& partition, const ManagedFile& file) noexcept
{
    FileHeader header;
    header.fill(' ');
    std::ranges::copy(std::string_view("PANGU-LOG-V1 "), header.begin());
    std::size_t offset = 13;
    for (const auto value : partition.Id)
    {
        Hex(std::span(header).subspan(offset, 2), std::to_integer<unsigned int>(value));
        offset += 2;
    }
    Hex(std::span(header).subspan(46, 16), file.Session);
    Hex(std::span(header).subspan(63, 16), file.Segment);
    header.back() = '\n';
    return header;
}
bool ParseManagedFile(const FilePartition& partition, std::string_view name, const FileHeader& header,
                      ManagedFile& file) noexcept
{
    if (name.size() != 40 || !name.starts_with("pg-") || name.substr(19, 1).front() != '-' || !name.ends_with(".log"))
    {
        return false;
    }
    const auto session = name.substr(3, 16);
    const auto segment = name.substr(20, 16);
    // from_chars 要求指针对；两个切片已验证为固定 16 字节。
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    auto const first = std::from_chars(session.data(), session.data() + session.size(), file.Session, 16);
    auto const second = std::from_chars(segment.data(), segment.data() + segment.size(), file.Segment, 16);
    if (first.ec != std::errc{} || first.ptr != session.data() + session.size() || second.ec != std::errc{} ||
        second.ptr != segment.data() + segment.size() || file.Session == 0 || file.Segment == 0)
    {
        return false;
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    SetName(file);
    return name == file.Name.data() && header == MakeFileHeader(partition, file);
}
bool JoinFilePath(const LogPath& directory, std::string_view name, LogPath& result) noexcept
{
    if (directory.Size >= kLogPathCapacity || name.size() > kLogPathCapacity - directory.Size - 1)
    {
        return false;
    }
    result = directory;
    auto tail = std::span(result.Bytes).subspan(result.Size);
    tail.front() = static_cast<std::filesystem::path::value_type>('/');
    tail = tail.subspan(1);
    for (const char value : name)
    {
        tail.front() = static_cast<std::filesystem::path::value_type>(value);
        tail = tail.subspan(1);
    }
    tail.front() = {};
    result.Size += 1 + name.size();
    return true;
}
FileStore::~FileStore()
{
    [[maybe_unused]] const auto cleanup = Close();
    PG_CHECK(!Inspect().CleanupPending, "日志文件分区仍有未释放资源");
}
Status FileStore::Fail(Error error) noexcept
{
    if (!State.Failure)
    {
        State.Failure = error;
    }
    State.Failed = true;
    return std::unexpected(*State.Failure);
}
Status FileStore::Open(const LogConfig& config, LogInitReport& report) noexcept
{
    if (Opened || Closed)
    {
        return std::unexpected(MakeLogError(LogError::kInvalidState));
    }
    Opened = true;
    const auto initialize = [&] -> Status
    {
        const auto valid = ValidateLogFileConfig(config);
        if (!valid)
        {
            return valid;
        }
        if (!HasOutput(config.Enabled, OutputMask::kFile))
        {
            return std::unexpected(MakeLogError(LogError::kInvalidConfig));
        }
        Partition.Directory.Size = config.Directory.size();
        std::ranges::copy(config.Directory, Partition.Directory.Bytes.begin());
        Partition.Id = config.PartitionId;
        PartitionLimit = config.PartitionBytes;
        FileLimit = config.FileBytes;
        LogPath maximumPath;
        if (!JoinFilePath(Partition.Directory, "pg-0000000000000001-0000000000000001.log", maximumPath))
        {
            return std::unexpected(MakeLogError(LogError::kInvalidConfig));
        }
        const auto opened = OpenPartition(Partition);
        if (!opened)
        {
            return opened;
        }
        const auto scan = ScanPartition(Partition, {});
        if (!scan)
        {
            return std::unexpected(scan.error());
        }
        State.ManagedBytes = scan->Bytes;
        if (scan->MaxSession == std::numeric_limits<std::uint64_t>::max())
        {
            return std::unexpected(MakeLogError(LogError::kSequenceExhausted));
        }
        Current.Session = scan->MaxSession + 1;
        return Rotate();
    };
    auto result = initialize();
    if (!result)
    {
        result = Fail(result.error());
        [[maybe_unused]] const auto cleanup = Close();
    }
    report.File = Inspect();
    report.FilePath = State.ActivePath;
    report.CleanupPending = report.CleanupPending || (!result && report.File.CleanupPending);
    return result;
}
Status CloseDeletedSegment(NativeHandle& handle, NativeIoFaults* faults) noexcept
{
    if (!handle.Valid())
    {
        return {};
    }
    if ((faults != nullptr) && faults->DeleteCloseFailure && faults->DeleteCloseRetainsResource)
    {
        return std::unexpected(*faults->DeleteCloseFailure);
    }
    const auto closed = CloseNative(handle);
    if (!closed)
    {
        return closed;
    }
    if ((faults != nullptr) && faults->DeleteCloseFailure)
    {
        return std::unexpected(*faults->DeleteCloseFailure);
    }
    return {};
}
Status FileStore::Reserve(std::uint64_t bytes) noexcept
{
    if (bytes > PartitionLimit)
    {
        return Fail(MakeLogError(LogError::kBudgetExceeded));
    }
    for (std::size_t attempt = 0; State.ManagedBytes > PartitionLimit - bytes; ++attempt)
    {
        if (attempt == kMaxDirectoryEntries)
        {
            return Fail(MakeLogError(LogError::kBudgetExceeded));
        }
        const auto scan = ScanPartition(Partition, Output.OwnsResource() ? Current.Name.data() : std::string_view{});
        if (!scan)
        {
            return Fail(scan.error());
        }
        if (!scan->Oldest)
        {
            return Fail(MakeLogError(LogError::kBudgetExceeded));
        }
        const auto removed = (Faults != nullptr) && Faults->DeleteFailure
                                 ? DeletionOutcome{.Failure = Faults->DeleteFailure}
                                 : DeleteSegment(Partition, *scan->Oldest, Faults);
        if (removed.Progress == DeletionProgress::kRemoved)
        {
            RecordReclaimed(scan->Oldest->Bytes);
        }
        else if (removed.Progress == DeletionProgress::kPending)
        {
            PendingReclaim = *scan->Oldest;
        }
        if (removed.Failure)
        {
            return Fail(*removed.Failure);
        }
        PG_CHECK(removed.Progress == DeletionProgress::kRemoved, "日志删除成功必须有实际回收进度");
    }
    return {};
}
void FileStore::RecordReclaimed(std::uint64_t bytes) noexcept
{
    State.ManagedBytes -= std::min(State.ManagedBytes, bytes);
    SaturatingAdd(State.ReclaimedBytes, bytes, State.CounterOverflow);
    SaturatingAdd(State.ReclaimedFiles, 1, State.CounterOverflow);
}
Status FileStore::Rotate() noexcept
{
    const auto closed = Output.Close();
    if (!closed)
    {
        return Fail(closed.error());
    }
    const auto reserved = Reserve(kLogFileHeaderCapacity);
    if (!reserved)
    {
        return reserved;
    }
    // 名称碰撞只尝试 32 次；不会覆盖外部同名文件。
    for (std::size_t attempt = 0; attempt < 32; ++attempt)
    {
        if (Current.Segment == std::numeric_limits<std::uint64_t>::max())
        {
            return Fail(MakeLogError(LogError::kSequenceExhausted));
        }
        ++Current.Segment;
        SetName(Current);
        const auto created = (Faults != nullptr) && Faults->CreateFailure
                                 ? Result<NativeHandle>(std::unexpected(*Faults->CreateFailure))
                                 : CreateSegment(Partition, Current.Name.data());
        if (!created)
        {
            if (IsNameCollision(created.error()))
            {
                continue;
            }
            return Fail(created.error());
        }
        Output.Adopt(*created);
        static_cast<void>(JoinFilePath(Partition.Directory, Current.Name.data(), State.ActivePath));
        const auto header = MakeFileHeader(Partition, Current);
        const auto written = Output.Write({header.data(), header.size()});
        Current.Bytes = written.BytesWritten;
        State.ManagedBytes += written.BytesWritten;
        if (!written.Complete)
        {
            return Fail(written.Failure.value_or(MakeLogError(LogError::kIoFailed)));
        }
        return {};
    }
    return Fail(MakeLogError(LogError::kFileOpenFailed));
}
OutputOutcome FileStore::Write(std::string_view text) noexcept
{
    if (State.Failure)
    {
        return {.Failure = State.Failure};
    }
    if (Closed || !Output.OwnsResource())
    {
        return {.Failure = MakeLogError(LogError::kInvalidState)};
    }
    if (text.size() > kEncodedLogLineCapacity)
    {
        [[maybe_unused]] const auto failed = Fail(MakeLogError(LogError::kInvalidConfig));
        return {.Failure = State.Failure};
    }
    if (text.size() > FileLimit - Current.Bytes)
    {
        if (!Rotate())
        {
            return {.Failure = State.Failure};
        }
    }
    if (!Reserve(text.size()))
    {
        return {.Failure = State.Failure};
    }
    auto result = Output.Write(text);
    Current.Bytes += result.BytesWritten;
    State.ManagedBytes += result.BytesWritten;
    if (!result.Complete)
    {
        [[maybe_unused]] const auto failed = Fail(result.Failure.value_or(MakeLogError(LogError::kIoFailed)));
    }
    return result;
}
OutputOutcome FileStore::Flush() noexcept
{
    if (State.Failure)
    {
        return {.Failure = State.Failure};
    }
    auto result = Output.Flush();
    if (!result.Complete)
    {
        [[maybe_unused]] const auto failed = Fail(result.Failure.value_or(MakeLogError(LogError::kIoFailed)));
    }
    return result;
}
Status FileStore::Close() noexcept
{
    Closed = true;
    auto status = Output.Close();
    if (!status)
    {
        State.CleanupFailure = status.error();
        [[maybe_unused]] const auto failed = Fail(status.error());
    }
    if (Output.OwnsResource())
    {
        return status;
    }
    if (PendingReclaim)
    {
        const auto deletion = FinishSegmentDeletion(Partition, *PendingReclaim, Faults);
        if (deletion.Progress == DeletionProgress::kRemoved)
        {
            // 即使关闭报告错误，已确认完成的删除也先记账。
            RecordReclaimed(PendingReclaim->Bytes);
            PendingReclaim.reset();
        }
        if (deletion.Failure)
        {
            State.CleanupFailure = deletion.Failure;
            status = std::unexpected(*deletion.Failure);
        }
        if (PendingReclaim)
        {
            // 外部句柄可能仍在使用；不等待，也不释放保护未完成删除的 lease/root。
            return status;
        }
    }
    const auto partition = ClosePartition(Partition, Faults);
    if (!partition)
    {
        State.CleanupFailure = partition.error();
        status = partition;
    }
    return status;
}
FileStoreSnapshot FileStore::Inspect() const noexcept
{
    FileStoreSnapshot snapshot;
    Inspect(snapshot);
    return snapshot;
}
void FileStore::Inspect(FileStoreSnapshot& snapshot) const noexcept
{
    snapshot = State;
    snapshot.LeaseHeld = Partition.Lease.Valid();
    snapshot.CleanupPending = PendingReclaim.has_value() || Output.OwnsResource() || Partition.Lease.Valid() ||
                              Partition.Root.Valid() || Partition.Scratch.Valid() || Partition.Cursor.Valid();
}
OutputPort FileStore::Port() noexcept
{
    return {
        .Context = this,
        .Write = [](void* context, std::string_view text) noexcept
        { return static_cast<FileStore*>(context)->Write(text); },
        .Flush = [](void* context) noexcept { return static_cast<FileStore*>(context)->Flush(); },
    };
}
} // namespace pg::log::detail
