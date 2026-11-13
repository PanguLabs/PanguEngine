#include "LogFileStore.h"
#include "Error.h"
#include "LogTypes.h"
#include "NativeLogIo.h"
#include "Result.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <expected>
#include <fcntl.h>
#include <limits>
#include <optional>
#include <sys/file.h> // IWYU pragma: keep
#include <sys/stat.h>
#include <tuple>
#include <unistd.h>
#include <utility> // IWYU pragma: keep
#ifdef __APPLE__
#include <sys/attr.h>
#include <sys/fcntl.h>
#include <sys/unistd.h>
#else
#include <sys/syscall.h>
#endif

namespace pg::log::detail
{
namespace
{
int Value(NativeHandle handle) noexcept { return static_cast<int>(handle.Value); }
Result<bool> ReadCandidate(FilePartition& partition, const char* name, ManagedFile& file) noexcept
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) 原生文件 API 的固定参数接缝。
    const int descriptor = openat(Value(partition.Root), name, O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0)
    {
        if (errno == ELOOP)
        {
            return false;
        }
        return std::unexpected(NativeIoError(errno));
    }
    partition.Scratch = {descriptor};
    struct stat info{};
    if (fstat(descriptor, &info) != 0)
    {
        return std::unexpected(NativeIoError(errno));
    }
    if (!S_ISREG(info.st_mode) || info.st_nlink != 1)
    {
        return false;
    }
    // 非等待文件锁再次证明候选不被参与协议的活跃写入者使用。
    if (flock(descriptor, LOCK_EX | LOCK_NB) != 0)
    {
        return std::unexpected(NativeIoError(errno, LogError::kBudgetExceeded));
    }
    FileHeader header{};
    const auto read = pread(descriptor, header.data(), header.size(), 0);
    if (read < 0)
    {
        return std::unexpected(NativeIoError(errno));
    }
    file.Bytes = static_cast<std::uint64_t>(info.st_size);
    return static_cast<std::size_t>(read) == header.size() && ParseManagedFile(partition, name, header, file);
}
} // namespace
Status OpenPartition(FilePartition& partition) noexcept
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) 原生文件 API 的固定参数接缝。
    const int root = open(partition.Directory.Bytes.data(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (root < 0)
    {
        return std::unexpected(NativeIoError(errno, LogError::kFileOpenFailed));
    }
    partition.Root = {root};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) 原生文件 API 的固定参数接缝。
    const int lease = openat(root, ".pangu-log.lease", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (lease < 0)
    {
        return std::unexpected(NativeIoError(errno, LogError::kFileOpenFailed));
    }
    partition.Scratch = {lease};
    struct stat info{};
    if (fstat(lease, &info) != 0)
    {
        return std::unexpected(NativeIoError(errno));
    }
    if (!S_ISREG(info.st_mode) || info.st_nlink != 1)
    {
        return std::unexpected(MakeLogError(LogError::kInvalidConfig));
    }
    if (flock(lease, LOCK_EX | LOCK_NB) != 0)
    {
        const int code = errno;
        return std::unexpected(NativeIoError(code, code == EWOULDBLOCK || code == EAGAIN ? LogError::kPartitionBusy
                                                                                         : LogError::kFileOpenFailed));
    }
    partition.Lease = partition.Scratch;
    partition.Scratch = {};
    return {};
}
Result<DirectoryScan> ScanPartition(FilePartition& partition, std::string_view active) noexcept
{
    DirectoryScan scan;
    // 直接使用固定目录记录缓冲，避免 opendir/readdir 隐藏的堆分配。
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) 原生文件 API 的固定参数接缝。
    const int cursor = openat(Value(partition.Root), ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (cursor < 0)
    {
        return std::unexpected(NativeIoError(errno));
    }
    partition.Cursor = {cursor};
    std::array<char, 8192> buffer{};
    std::size_t entries{};
    std::optional<Error> failure;
#ifdef __APPLE__
    struct attrlist attributes{};
    attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
    attributes.commonattr = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME;
#endif
    while (!failure)
    {
#ifdef __APPLE__
        const auto count = getattrlistbulk(cursor, &attributes, buffer.data(), buffer.size(), 0);
#else
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) 原生文件 API 的固定参数接缝。
        const auto count = syscall(SYS_getdents64, cursor, buffer.data(), buffer.size());
        // Linux getdents64 的固定 UAPI：两个 64 位字段、16 位长度、8 位类型。
        constexpr std::size_t kNameOffset = 19;
        constexpr std::size_t kLengthOffset = 16;
#endif
        if (count < 0)
        {
            failure = NativeIoError(errno);
            break;
        }
        if (count == 0)
        {
            break;
        }
        std::size_t offset{};
#ifdef __APPLE__
        for (int entry = 0; entry < count; ++entry)
#else
        while (std::cmp_less(offset, count))
#endif
        {
            if (++entries > kMaxDirectoryEntries)
            {
                failure = MakeLogError(LogError::kBudgetExceeded);
                break;
            }
#ifdef __APPLE__
            constexpr auto kReferenceOffset = sizeof(std::uint32_t) + sizeof(attribute_set_t);
            if (offset > buffer.size() || buffer.size() - offset < kReferenceOffset + sizeof(attrreference_t))
            {
                failure = MakeLogError(LogError::kIoFailed);
                break;
            }
            std::uint32_t length{};
            attribute_set_t returned{};
            attrreference_t reference{};
            // 已验证记录范围；原生打包 ABI 通过 memcpy 读取，避免未对齐的类型访问。
            // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            std::memcpy(&length, buffer.data() + offset, sizeof(length));
            std::memcpy(&returned, buffer.data() + offset + sizeof(length), sizeof(returned));
            std::memcpy(&reference, buffer.data() + offset + kReferenceOffset, sizeof(reference));
            if (length > buffer.size() - offset || length < kReferenceOffset + sizeof(reference) ||
                (returned.commonattr & ATTR_CMN_NAME) == 0 || reference.attr_dataoffset < 0)
            {
                failure = MakeLogError(LogError::kIoFailed);
                break;
            }
            const auto nameOffset = kReferenceOffset + static_cast<std::size_t>(reference.attr_dataoffset);
            if (nameOffset > length || reference.attr_length > length - nameOffset || reference.attr_length == 0)
            {
                failure = MakeLogError(LogError::kIoFailed);
                break;
            }
            const char* name = buffer.data() + offset + nameOffset;
            const auto capacity = static_cast<std::size_t>(reference.attr_length);
            // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
#else
            const auto remaining = static_cast<std::size_t>(count) - offset;
            if (remaining <= kNameOffset)
            {
                failure = MakeLogError(LogError::kIoFailed);
                break;
            }
            // Linux 记录长度和名字范围在读取前校验。
            // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            std::uint16_t length{};
            std::memcpy(&length, buffer.data() + offset + kLengthOffset, sizeof(length));
            if (length <= kNameOffset || length > remaining)
            {
                failure = MakeLogError(LogError::kIoFailed);
                break;
            }
            const char* name = buffer.data() + offset + kNameOffset;
            const auto capacity = static_cast<std::size_t>(length) - kNameOffset;
            // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
#endif
            const auto* end = static_cast<const char*>(std::memchr(name, 0, capacity));
            if (end == nullptr)
            {
                failure = MakeLogError(LogError::kIoFailed);
                break;
            }
            const std::string_view view(name, static_cast<std::size_t>(end - name));
            if (view.size() == 40 && view.starts_with("pg-") && view.ends_with(".log") && view != active)
            {
                ManagedFile file;
                const auto candidate = ReadCandidate(partition, name, file);
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
                    scan.MaxSession = std::max(scan.MaxSession, file.Session);
                    if (!scan.Oldest ||
                        std::tie(file.Session, file.Segment) < std::tie(scan.Oldest->Session, scan.Oldest->Segment))
                    {
                        scan.Oldest = file;
                    }
                }
            }
            offset += length;
        }
    }
    const auto closed = CloseNative(partition.Cursor);
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
Result<NativeHandle> CreateSegment(FilePartition& partition, std::string_view name) noexcept
{
    FileName terminated{};
    if (name.size() >= terminated.size())
    {
        return std::unexpected(MakeLogError(LogError::kInvalidConfig));
    }
    std::ranges::copy(name, terminated.begin());
    const int descriptor =
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg) 原生 exclusive create 的 mode 参数。
        openat(Value(partition.Root), terminated.data(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0)
    {
        return std::unexpected(NativeIoError(errno, LogError::kFileOpenFailed));
    }
    // 每个活跃段也持有锁；即使租约因外部管理提前结束，扫描也不能回收活跃文件。
    partition.Scratch = {descriptor};
    if (flock(descriptor, LOCK_EX | LOCK_NB) != 0)
    {
        return std::unexpected(NativeIoError(errno));
    }
    partition.Scratch = {};
    return NativeHandle{descriptor};
}
DeletionOutcome DeleteSegment(FilePartition& partition, const ManagedFile& file, NativeIoFaults* faults) noexcept
{
    ManagedFile verified;
    const auto candidate = ReadCandidate(partition, file.Name.data(), verified);
    if (!candidate)
    {
        return {.Failure = candidate.error()};
    }
    if (!*candidate || verified.Bytes != file.Bytes)
    {
        return {.Failure = MakeLogError(LogError::kIoFailed)};
    }
    struct stat opened{};
    struct stat named{};
    if (fstat(Value(partition.Scratch), &opened) != 0 ||
        fstatat(Value(partition.Root), file.Name.data(), &named, AT_SYMLINK_NOFOLLOW) != 0)
    {
        return {.Failure = NativeIoError(errno)};
    }
    if (opened.st_ino != named.st_ino || opened.st_dev != named.st_dev || named.st_nlink != 1)
    {
        return {.Failure = MakeLogError(LogError::kIoFailed)};
    }
    // 分区租约排除协作写入者；无权限隔离的恶意目录替换不属于日志分区协议。
    if (unlinkat(Value(partition.Root), file.Name.data(), 0) != 0)
    {
        return {.Failure = NativeIoError(errno)};
    }
    return FinishSegmentDeletion(partition, file, faults);
}
DeletionOutcome FinishSegmentDeletion(FilePartition& partition, [[maybe_unused]] const ManagedFile& file,
                                      NativeIoFaults* faults) noexcept
{
    const auto closed = CloseDeletedSegment(partition.Scratch, faults);
    // unlinkat 已移除唯一目录项，close 的错误或残留 fd 不撤销该副作用。
    return {.Progress = DeletionProgress::kRemoved, .Failure = closed ? std::nullopt : std::optional(closed.error())};
}
Status ClosePartition(FilePartition& partition, NativeIoFaults* faults) noexcept
{
    const auto scratch = CloseDeletedSegment(partition.Scratch, faults);
    const auto cursor = CloseNative(partition.Cursor);
    if (partition.Scratch.Valid() || partition.Cursor.Valid())
    {
        return !scratch ? scratch : cursor;
    }
    const auto lease = CloseNative(partition.Lease);
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
bool IsNameCollision(const Error& error) noexcept { return error.Native() && error.Native()->Value == EEXIST; }
} // namespace pg::log::detail
