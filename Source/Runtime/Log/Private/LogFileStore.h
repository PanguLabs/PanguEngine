#pragma once

#include "NativeLogIo.h"
#include <LogReports.h>
#include <cstdint>

namespace pg::log::detail
{
inline constexpr std::size_t kFileNameCapacity = 64;
inline constexpr std::size_t kMaxDirectoryEntries = 65536;
using FileName = std::array<char, kFileNameCapacity>;
using FileHeader = std::array<char, kLogFileHeaderCapacity>;

struct ManagedFile
{
    FileName Name{};
    std::uint64_t Session{};
    std::uint64_t Segment{};
    std::uint64_t Bytes{};
};
struct DirectoryScan
{
    std::uint64_t Bytes{};
    std::uint64_t MaxSession{};
    std::optional<ManagedFile> Oldest;
};
enum class DeletionProgress : std::uint8_t
{
    kUnchanged,
    kPending,
    kRemoved,
};
struct DeletionOutcome
{
    // 删除副作用和关闭错误独立；残留资源责任由 FilePartition 的句柄持有。
    DeletionProgress Progress{DeletionProgress::kUnchanged};
    std::optional<Error> Failure;
};
struct FilePartition
{
    LogPath Directory;
    std::array<std::byte, 16> Id{};
    NativeHandle Root;
    NativeHandle Lease;
    NativeHandle Scratch;
    NativeHandle Cursor;
};

[[nodiscard]] PG_LOG_API FileHeader MakeFileHeader(const FilePartition& partition, const ManagedFile& file) noexcept;
[[nodiscard]] PG_LOG_API bool ParseManagedFile(const FilePartition& partition, std::string_view name,
                                               const FileHeader& header, ManagedFile& file) noexcept;
[[nodiscard]] PG_LOG_API bool JoinFilePath(const LogPath& directory, std::string_view name, LogPath& result) noexcept;
[[nodiscard]] PG_LOG_API Status OpenPartition(FilePartition& partition) noexcept;
[[nodiscard]] PG_LOG_API Result<DirectoryScan> ScanPartition(FilePartition& partition,
                                                             std::string_view active) noexcept;
[[nodiscard]] PG_LOG_API Result<NativeHandle> CreateSegment(FilePartition& partition, std::string_view name) noexcept;
[[nodiscard]] PG_LOG_API DeletionOutcome DeleteSegment(FilePartition& partition, const ManagedFile& file,
                                                       NativeIoFaults* faults = nullptr) noexcept;
[[nodiscard]] PG_LOG_API DeletionOutcome FinishSegmentDeletion(FilePartition& partition, const ManagedFile& file,
                                                               NativeIoFaults* faults) noexcept;
[[nodiscard]] PG_LOG_API Status CloseDeletedSegment(NativeHandle& handle, NativeIoFaults* faults) noexcept;
[[nodiscard]] PG_LOG_API Status ClosePartition(FilePartition& partition, NativeIoFaults* faults = nullptr) noexcept;
[[nodiscard]] PG_LOG_API bool IsNameCollision(const Error& error) noexcept;

// 单一消费者串行使用；拥有租约与当前文件，失败不重开，Close 可重试残留责任。
class PG_LOG_API FileStore final
{
public:
    explicit FileStore(NativeIoFaults* faults = nullptr) noexcept
        : Faults(faults)
        , Output(faults)
    {
    }
    ~FileStore();
    FileStore(const FileStore&) = delete;
    FileStore& operator=(const FileStore&) = delete;
    FileStore(FileStore&&) = delete;
    FileStore& operator=(FileStore&&) = delete;
    [[nodiscard]] Status Open(const LogConfig& config, LogInitReport& report) noexcept;
    [[nodiscard]] OutputOutcome Write(std::string_view text) noexcept;
    [[nodiscard]] OutputOutcome Flush() noexcept;
    [[nodiscard]] Status Close() noexcept;
    [[nodiscard]] FileStoreSnapshot Inspect() const noexcept;
    // 直接填写调用方拥有的报告，避免大路径快照的中转副本。
    void Inspect(FileStoreSnapshot& snapshot) const noexcept;
    [[nodiscard]] OutputPort Port() noexcept;

private:
    [[nodiscard]] Status Reserve(std::uint64_t bytes) noexcept;
    [[nodiscard]] Status Rotate() noexcept;
    [[nodiscard]] Status Fail(Error error) noexcept;
    void RecordReclaimed(std::uint64_t bytes) noexcept;
    FilePartition Partition;
    NativeIoFaults* Faults{};
    NativeLogIo Output;
    FileStoreSnapshot State;
    ManagedFile Current;
    // Windows disposition 已成功、目录项尚未确认移除；完成后仅补记一次。
    std::optional<ManagedFile> PendingReclaim;
    std::uint64_t PartitionLimit{};
    std::uint64_t FileLimit{};
    bool Opened{};
    bool Closed{};
};
} // namespace pg::log::detail
