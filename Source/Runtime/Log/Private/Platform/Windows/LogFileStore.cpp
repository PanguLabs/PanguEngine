#include "LogFileStore.h"
#include "Error.h"
#include "LogReports.h"
#include "LogTypes.h"
#include "NativeLogIo.h"
#include "Result.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h> // IWYU pragma: keep
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <errhandlingapi.h>
#include <expected>
#include <fileapi.h>
#include <handleapi.h>
#include <limits>
#include <minwinbase.h>
#include <minwindef.h>
#include <optional>
#include <string_view>
#include <tuple>
#include <winbase.h>
#include <winerror.h>
#include <winnt.h>

namespace pg::log::detail
{
namespace
{
HANDLE Value(NativeHandle handle) noexcept
{
    // 不透明 Win32 句柄仅在平台边界转换。
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
    return reinterpret_cast<HANDLE>(handle.Value);
}
NativeHandle Store(HANDLE handle) noexcept
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) 原生句柄保存到私有平台无关存储。
    return {reinterpret_cast<std::intptr_t>(handle)};
}
Status ErrorStatus(LogError kind = LogError::kIoFailed) noexcept
{
    return std::unexpected(NativeIoError(GetLastError(), kind));
}
Result<bool> RegularSingleLink(HANDLE handle, BY_HANDLE_FILE_INFORMATION& info) noexcept
{
    if (GetFileInformationByHandle(handle, &info) == 0)
    {
        return std::unexpected(NativeIoError(GetLastError()));
    }
    // NOLINTNEXTLINE(bugprone-signed-bitwise) SDK 的非负位集合宏。
    return (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0 &&
           info.nNumberOfLinks == 1;
}
Status CloseCursor(FilePartition& partition) noexcept
{
    if (!partition.Cursor.Valid())
    {
        return {};
    }
    if (FindClose(Value(partition.Cursor)) == 0)
    {
        return ErrorStatus();
    }
    partition.Cursor = {};
    return {};
}
Result<bool> ReadCandidate(FilePartition& partition, std::string_view name, ManagedFile& file, bool deleting) noexcept
{
    LogPath path;
    if (!JoinFilePath(partition.Directory, name, path))
    {
        return false;
    }
    // NOLINTNEXTLINE(bugprone-signed-bitwise) SDK 的 DELETE 权限位与 GENERIC_READ 组合。
    auto* handle = CreateFileW(path.Bytes.data(), GENERIC_READ | (deleting ? DELETE : 0), FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return std::unexpected(NativeIoError(GetLastError()));
    }
    partition.Scratch = Store(handle);
    BY_HANDLE_FILE_INFORMATION info{};
    FileHeader header{};
    DWORD count{};
    const auto regular = RegularSingleLink(handle, info);
    if (!regular)
    {
        return std::unexpected(regular.error());
    }
    bool owned = *regular;
    if (owned)
    {
        if (ReadFile(handle, header.data(), static_cast<DWORD>(header.size()), &count, nullptr) == 0)
        {
            return std::unexpected(NativeIoError(GetLastError()));
        }
        owned = count == header.size() && ParseManagedFile(partition, name, header, file);
    }
    file.Bytes = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32U) | info.nFileSizeLow;
    return owned;
}
} // namespace
Status OpenPartition(FilePartition& partition) noexcept
{
    auto* root =
        CreateFileW(partition.Directory.Bytes.data(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (root == INVALID_HANDLE_VALUE)
    {
        return ErrorStatus(LogError::kFileOpenFailed);
    }
    partition.Root = Store(root);
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileInformationByHandle(root, &info) == 0)
    {
        return ErrorStatus();
    }
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        return std::unexpected(MakeLogError(LogError::kInvalidConfig));
    }
    LogPath path;
    if (!JoinFilePath(partition.Directory, ".pangu-log.lease", path))
    {
        return std::unexpected(MakeLogError(LogError::kInvalidConfig));
    }
    auto* lease = CreateFileW(path.Bytes.data(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (lease == INVALID_HANDLE_VALUE)
    {
        return ErrorStatus(LogError::kFileOpenFailed);
    }
    partition.Scratch = Store(lease);
    const auto regular = RegularSingleLink(lease, info);
    if (!regular)
    {
        return std::unexpected(regular.error());
    }
    if (!*regular)
    {
        return std::unexpected(MakeLogError(LogError::kInvalidConfig));
    }
    OVERLAPPED overlap{};
    if (LockFileEx(lease, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlap) == 0)
    {
        const auto code = GetLastError();
        return std::unexpected(
            NativeIoError(code, code == ERROR_LOCK_VIOLATION ? LogError::kPartitionBusy : LogError::kFileOpenFailed));
    }
    partition.Lease = partition.Scratch;
    partition.Scratch = {};
    return {};
}
Result<DirectoryScan> ScanPartition(FilePartition& partition, std::string_view active) noexcept
{
    DirectoryScan scan;
    LogPath pattern;
    if (!JoinFilePath(partition.Directory, "*", pattern))
    {
        return std::unexpected(MakeLogError(LogError::kInvalidConfig));
    }
    WIN32_FIND_DATAW data{};
    auto* cursor = FindFirstFileExW(pattern.Bytes.data(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (cursor == INVALID_HANDLE_VALUE)
    {
        return std::unexpected(NativeIoError(GetLastError()));
    }
    partition.Cursor = Store(cursor);
    std::optional<Error> failure;
    for (std::size_t entries = 0;; ++entries)
    {
        if (entries == kMaxDirectoryEntries)
        {
            failure = MakeLogError(LogError::kBudgetExceeded);
            break;
        }
        FileName name{};
        std::size_t length{};
        // 索引最多 63，小于 FileName 64 和 Win32 cFileName 260；只读取 ASCII 文件名。
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        while (length < name.size() - 1 && data.cFileName[length] > 0 && data.cFileName[length] < 128)
        {
            name[length] = static_cast<char>(data.cFileName[length]);
            ++length;
        }
        const std::string_view view(name.data(), length);
        if (data.cFileName[length] == 0 && view.size() == 40 && view.starts_with("pg-") && view.ends_with(".log") &&
            // NOLINTNEXTLINE(bugprone-signed-bitwise) SDK 非负属性掩码。
            (data.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0 && view != active)
        {
            // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
            ManagedFile file;
            const auto candidate = ReadCandidate(partition, view, file, false);
            if (!candidate)
            {
                failure = candidate.error();
                break;
            }
            const auto closed = CloseNative(partition.Scratch);
            if (!closed)
            {
                failure = closed.error();
                break;
            }
            if (*candidate)
            {
                if (file.Bytes > std::numeric_limits<std::uint64_t>::max() - scan.Bytes)
                {
                    failure = MakeLogError(LogError::kBudgetExceeded);
                    break;
                }
                scan.Bytes += file.Bytes;
                scan.MaxSession = (std::max)(scan.MaxSession, file.Session);
                if (!scan.Oldest ||
                    std::tie(file.Session, file.Segment) < std::tie(scan.Oldest->Session, scan.Oldest->Segment))
                {
                    scan.Oldest = file;
                }
            }
        }
        if (FindNextFileW(cursor, &data) == 0)
        {
            const auto code = GetLastError();
            if (code != ERROR_NO_MORE_FILES)
            {
                failure = NativeIoError(code);
            }
            break;
        }
    }
    const auto closed = CloseCursor(partition);
    if (!failure && !closed)
    {
        failure = closed.error();
    }
    if (failure)
    {
        return std::unexpected(*failure);
    }
    return scan;
}
// NOLINTNEXTLINE(misc-const-correctness) 与 POSIX 失败时保存 Scratch 责任的接口一致。
Result<NativeHandle> CreateSegment(FilePartition& partition, std::string_view name) noexcept
{
    LogPath path;
    if (!JoinFilePath(partition.Directory, name, path))
    {
        return std::unexpected(MakeLogError(LogError::kInvalidConfig));
    }
    auto* handle = CreateFileW(path.Bytes.data(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return std::unexpected(NativeIoError(GetLastError(), LogError::kFileOpenFailed));
    }
    return Store(handle);
}
DeletionOutcome DeleteSegment(FilePartition& partition, const ManagedFile& file, NativeIoFaults* faults) noexcept
{
    ManagedFile verified;
    const auto candidate = ReadCandidate(partition, file.Name.data(), verified, true);
    if (!candidate)
    {
        return {.Failure = candidate.error()};
    }
    if (!*candidate || verified.Bytes != file.Bytes)
    {
        return {.Failure = MakeLogError(LogError::kIoFailed)};
    }
    FILE_DISPOSITION_INFO disposition{TRUE};
    if (SetFileInformationByHandle(Value(partition.Scratch), FileDispositionInfo, &disposition, sizeof(disposition)) ==
        0)
    {
        return {.Failure = NativeIoError(GetLastError())};
    }
    return FinishSegmentDeletion(partition, file, faults);
}
DeletionOutcome FinishSegmentDeletion(FilePartition& partition, const ManagedFile& file,
                                      NativeIoFaults* faults) noexcept
{
    const auto closed = CloseDeletedSegment(partition.Scratch, faults);
    const std::optional<Error> failure = closed ? std::nullopt : std::optional(closed.error());
    if (partition.Scratch.Valid())
    {
        return {.Progress = DeletionProgress::kPending, .Failure = failure};
    }
    LogPath path;
    if (!JoinFilePath(partition.Directory, file.Name.data(), path))
    {
        return {
            .Progress = DeletionProgress::kPending,
            .Failure = failure.value_or(MakeLogError(LogError::kInvalidConfig)),
        };
    }
    // FILE_SHARE_READ 仍允许已存在的共享只读/零访问句柄延迟删除；自身 close 不证明完成。
    const auto attributes = GetFileAttributesW(path.Bytes.data());
    const auto code = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_DELETE_PENDING;
    if (attributes == INVALID_FILE_ATTRIBUTES && code == ERROR_FILE_NOT_FOUND)
    {
        return {.Progress = DeletionProgress::kRemoved, .Failure = failure};
    }
    return {.Progress = DeletionProgress::kPending, .Failure = failure.value_or(NativeIoError(code))};
}
Status ClosePartition(FilePartition& partition, NativeIoFaults* faults) noexcept
{
    const auto scratch = CloseDeletedSegment(partition.Scratch, faults);
    const auto cursor = CloseCursor(partition);
    if (partition.Scratch.Valid() || partition.Cursor.Valid())
    {
        return !scratch ? scratch : cursor;
    }
    const auto lease = CloseNative(partition.Lease);
    if (partition.Lease.Valid())
    {
        return lease;
    }
    const auto root = CloseNative(partition.Root);
    if (!scratch)
    {
        return scratch;
    }
    if (!cursor)
    {
        return cursor;
    }
    return !lease ? lease : root;
}
bool IsNameCollision(const Error& error) noexcept
{
    return error.Native() &&
           (error.Native()->Value == ERROR_FILE_EXISTS || error.Native()->Value == ERROR_ALREADY_EXISTS);
}
} // namespace pg::log::detail
