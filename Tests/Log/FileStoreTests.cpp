#include "LogReports.h"
#include "NativeLogIo.h"
#include <LogFileStore.h>
#include <algorithm>
#include <cstddef>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#ifdef _WIN32
#include <Windows.h> // IWYU pragma: keep
#include <cstdint>
#include <fileapi.h>
#include <handleapi.h>
#include <minwindef.h>
#include <winnt.h>
#endif

namespace
{
using pg::log::detail::FileStore;
class LogFiles : public testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = testing::UnitTest::GetInstance()->current_test_info();
        Directory = std::filesystem::path(PG_LOG_FILE_TEST_ROOT) / info->name();
        std::error_code error;
        const auto root = std::filesystem::weakly_canonical(PG_LOG_FILE_TEST_ROOT, error);
        ASSERT_FALSE(error);
        const auto resolved = std::filesystem::weakly_canonical(Directory, error);
        ASSERT_FALSE(error);
        ASSERT_EQ(resolved.parent_path(), root);
        // 仅删除由固定 BuildRoot 与当前测试名组成的本测试目录。
        std::filesystem::remove_all(Directory, error);
        ASSERT_FALSE(error);
        ASSERT_TRUE(std::filesystem::create_directories(Directory, error));
        Config.Directory = Directory.native();
        Config.PartitionId.front() = std::byte{1};
        Config.FileBytes = 13312;
        Config.PartitionBytes = 26624;
    }
#ifdef _WIN32
    [[nodiscard]] bool ContainsEntry(const std::filesystem::path& path) const
    {
        // Windows delete-pending 路径拒绝重新打开，以目录项判断是否仍未移除。
        return std::ranges::any_of(std::filesystem::directory_iterator(Directory),
                                   [&](const auto& entry) { return entry.path() == path; });
    }
    void VerifyDeletionWaitsForExternalHandle(DWORD access)
    {
        FileStore store;
        ASSERT_TRUE(store.Open(Config, Report));
        const auto oldest = std::filesystem::path(store.Inspect().ActivePath.View());
        const std::string line(12288, 'x');
        ASSERT_TRUE(store.Write(line).Complete);
        ASSERT_TRUE(store.Write(line).Complete);
        // 外部只读工具允许 DELETE 共享，自身句柄仍能延迟最后的删除完成。
        // NOLINTNEXTLINE(bugprone-signed-bitwise) SDK 的非负共享模式位集合。
        auto* handle = CreateFileW(oldest.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        ASSERT_NE(handle, INVALID_HANDLE_VALUE);
        pg::log::detail::NativeLogIo reader;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) 原生测试句柄转入既有 RAII 所有者。
        reader.Adopt({reinterpret_cast<std::intptr_t>(handle)});
        EXPECT_FALSE(store.Write(line).Complete);
        EXPECT_TRUE(ContainsEntry(oldest));
        EXPECT_EQ(store.Inspect().ManagedBytes, 26624U);
        EXPECT_EQ(store.Inspect().ReclaimedBytes, 0U);
        EXPECT_EQ(store.Inspect().ReclaimedFiles, 0U);
        EXPECT_TRUE(store.Inspect().Failed);
        const auto primary = store.Inspect().Failure;
        EXPECT_FALSE(store.Close());
        EXPECT_TRUE(store.Inspect().LeaseHeld);
        EXPECT_TRUE(store.Inspect().CleanupPending);
        EXPECT_EQ(store.Inspect().ManagedBytes, 26624U);
        EXPECT_EQ(store.Inspect().ReclaimedFiles, 0U);
        FileStore competitor;
        EXPECT_FALSE(competitor.Open(Config, Report));
        EXPECT_TRUE(reader.Close());
        EXPECT_FALSE(ContainsEntry(oldest));
        EXPECT_TRUE(store.Close());
        EXPECT_TRUE(store.Close());
        EXPECT_EQ(store.Inspect().ManagedBytes, 13312U);
        EXPECT_EQ(store.Inspect().ReclaimedBytes, 13312U);
        EXPECT_EQ(store.Inspect().ReclaimedFiles, 1U);
        EXPECT_FALSE(store.Inspect().LeaseHeld);
        EXPECT_FALSE(store.Inspect().CleanupPending);
        EXPECT_EQ(store.Inspect().Failure, primary);
        EXPECT_FALSE(store.Write("still failed").Complete);
    }

#endif
    // GTest 派生用例共享夹具状态。
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    std::filesystem::path Directory;
    pg::log::LogConfig Config;
    pg::log::LogInitReport Report;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

TEST_F(LogFiles, ExclusiveAcrossProcessesAndHosts)
{
    FileStore first;
    ASSERT_TRUE(first.Open(Config, Report));
    FileStore second;
    const auto result = second.Open(Config, Report);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().Code(), 6U);
    EXPECT_TRUE(first.Inspect().LeaseHeld);
    EXPECT_FALSE(second.Inspect().LeaseHeld);
}

TEST_F(LogFiles, StaleProcessLeaseCanBeReacquired)
{
    {
        FileStore first;
        ASSERT_TRUE(first.Open(Config, Report));
    }
    FileStore second;
    ASSERT_TRUE(second.Open(Config, Report));
    EXPECT_TRUE(second.Inspect().LeaseHeld);
}

TEST_F(LogFiles, ForeignMatchingNamePreserved)
{
    const auto foreign = Directory / "pg-0000000000000001-0000000000000001.log";
    {
        std::ofstream output(foreign);
        output << "external file";
    }
    const auto linked = Directory / "pg-0000000000000001-0000000000000002.log";
    std::error_code error;
    std::filesystem::create_hard_link(foreign, linked, error);
    ASSERT_FALSE(error);
    FileStore store;
    ASSERT_TRUE(store.Open(Config, Report));
    const std::string line(12288, 'a');
    for (int index = 0; index < 5; ++index)
    {
        ASSERT_TRUE(store.Write(line).Complete);
    }
    EXPECT_TRUE(std::filesystem::exists(foreign));
    EXPECT_TRUE(std::filesystem::exists(linked));
    EXPECT_EQ(std::filesystem::file_size(foreign), 13U);
}

TEST_F(LogFiles, NoDeleteOfLiveFile)
{
    FileStore store;
    ASSERT_TRUE(store.Open(Config, Report));
    const auto active = std::filesystem::path(store.Inspect().ActivePath.View());
    const std::string line(12288, 'x');
    ASSERT_TRUE(store.Write(line).Complete);
    FileStore competitor;
    EXPECT_FALSE(competitor.Open(Config, Report));
    EXPECT_TRUE(std::filesystem::exists(active));
    EXPECT_EQ(std::filesystem::file_size(active), 13312U);
}

TEST_F(LogFiles, LiveManagedHistoryCannotBeReclaimedWithoutFileLock)
{
    pg::log::detail::FilePartition partition;
    partition.Directory.Size = Config.Directory.size();
    std::ranges::copy(Config.Directory, partition.Directory.Bytes.begin());
    partition.Id = Config.PartitionId;
    ASSERT_TRUE(pg::log::detail::OpenPartition(partition));
    constexpr std::string_view kName = "pg-0000000000000001-0000000000000001.log";
    const auto created = pg::log::detail::CreateSegment(partition, kName);
    ASSERT_TRUE(created);
    pg::log::detail::NativeLogIo active;
    active.Adopt(*created);
    pg::log::detail::ManagedFile file;
    file.Session = 1;
    file.Segment = 1;
    const auto header = pg::log::detail::MakeFileHeader(partition, file);
    ASSERT_TRUE(active.Write({header.data(), header.size()}).Complete);
    ASSERT_TRUE(pg::log::detail::ClosePartition(partition));
    // 保持文件活跃但释放目录租约，验证回收还有独立的文件存活保护。
    FileStore store;
    EXPECT_FALSE(store.Open(Config, Report));
    EXPECT_TRUE(std::filesystem::exists(Directory / kName));
}

TEST_F(LogFiles, QuotaIncludesHistory)
{
    const std::string line(12288, 'x');
    {
        FileStore first;
        ASSERT_TRUE(first.Open(Config, Report));
        ASSERT_TRUE(first.Write(line).Complete);
    }
    FileStore second;
    ASSERT_TRUE(second.Open(Config, Report));
    ASSERT_TRUE(second.Write(line).Complete);
    ASSERT_TRUE(second.Write(line).Complete);
    const auto state = second.Inspect();
    EXPECT_LE(state.ManagedBytes, Config.PartitionBytes);
    EXPECT_EQ(state.ManagedBytes, 26624U);
    EXPECT_EQ(state.ReclaimedBytes, 13312U);
    EXPECT_EQ(state.ReclaimedFiles, 1U);
}

TEST_F(LogFiles, RotateAtWholeRecord)
{
    FileStore store;
    ASSERT_TRUE(store.Open(Config, Report));
    const auto first = std::filesystem::path(store.Inspect().ActivePath.View());
    ASSERT_TRUE(store.Write(std::string(10000, 'a')).Complete);
    ASSERT_TRUE(store.Write(std::string(4000, 'b')).Complete);
    const auto second = std::filesystem::path(store.Inspect().ActivePath.View());
    EXPECT_NE(first, second);
    EXPECT_EQ(std::filesystem::file_size(first), 11024U);
    EXPECT_EQ(std::filesystem::file_size(second), 5024U);
}

TEST_F(LogFiles, CloseOrDeleteFailureDegrades)
{
    pg::log::detail::NativeIoFaults faults;
    FileStore store(&faults);
    ASSERT_TRUE(store.Open(Config, Report));
    ASSERT_TRUE(store.Write(std::string(12288, 'a')).Complete);
    faults.CloseFailure = pg::log::detail::NativeIoError(123);
    EXPECT_FALSE(store.Write("next").Complete);
    EXPECT_TRUE(store.Inspect().Failed);
    const auto state = store.Inspect();
    ASSERT_TRUE(state.Failure);
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) 上方 ASSERT_TRUE 失败会退出当前 GTest 用例。
    const auto native = state.Failure->Native();
    ASSERT_TRUE(native);
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) 已通过 ASSERT_TRUE(native)。
    EXPECT_EQ(native->Value, 123);
    EXPECT_TRUE(store.Close());
    EXPECT_FALSE(store.Inspect().CleanupPending);
}

TEST_F(LogFiles, DeleteFailureRetainsHistoryAndStopsFile)
{
    pg::log::detail::NativeIoFaults faults;
    FileStore store(&faults);
    ASSERT_TRUE(store.Open(Config, Report));
    const std::string line(12288, 'x');
    ASSERT_TRUE(store.Write(line).Complete);
    ASSERT_TRUE(store.Write(line).Complete);
    faults.DeleteFailure = pg::log::detail::NativeIoError(124);
    EXPECT_FALSE(store.Write(line).Complete);
    const auto state = store.Inspect();
    ASSERT_TRUE(state.Failure);
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) 上方 ASSERT_TRUE 失败会退出当前 GTest 用例。
    const auto native = state.Failure->Native();
    ASSERT_TRUE(native);
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) 已通过 ASSERT_TRUE(native)。
    EXPECT_EQ(native->Value, 124);
    EXPECT_EQ(store.Inspect().ReclaimedFiles, 0U);
    EXPECT_LE(store.Inspect().ManagedBytes, Config.PartitionBytes);
}

TEST_F(LogFiles, DeleteThenCloseFailureAccountsReclamationOnce)
{
    pg::log::detail::NativeIoFaults faults;
    FileStore store(&faults);
    ASSERT_TRUE(store.Open(Config, Report));
    const auto oldest = std::filesystem::path(store.Inspect().ActivePath.View());
    const std::string line(12288, 'x');
    ASSERT_TRUE(store.Write(line).Complete);
    ASSERT_TRUE(store.Write(line).Complete);
    const auto newer = std::filesystem::path(store.Inspect().ActivePath.View());
    faults.DeleteCloseFailure = pg::log::detail::NativeIoError(130);
    const auto failed = store.Write(line);
    EXPECT_FALSE(failed.Complete);
    EXPECT_EQ(failed.BytesWritten, 0U);
    EXPECT_EQ(failed.Failure, faults.DeleteCloseFailure);
    EXPECT_FALSE(std::filesystem::exists(oldest));
    EXPECT_EQ(std::filesystem::file_size(newer), 13312U);
    const auto state = store.Inspect();
    EXPECT_EQ(state.ManagedBytes, 13312U);
    EXPECT_EQ(state.ReclaimedBytes, 13312U);
    EXPECT_EQ(state.ReclaimedFiles, 1U);
    EXPECT_TRUE(state.Failed);
    EXPECT_EQ(state.Failure, faults.DeleteCloseFailure);
    EXPECT_TRUE(state.CleanupPending);
    EXPECT_TRUE(state.LeaseHeld);
    FileStore competitor;
    EXPECT_FALSE(competitor.Open(Config, Report));
    faults.DeleteCloseFailure.reset();
    EXPECT_FALSE(store.Write("still failed").Complete);
    EXPECT_TRUE(store.Close());
    EXPECT_TRUE(store.Close());
    const auto closed = store.Inspect();
    EXPECT_EQ(closed.ManagedBytes, 13312U);
    EXPECT_EQ(closed.ReclaimedBytes, 13312U);
    EXPECT_EQ(closed.ReclaimedFiles, 1U);
    EXPECT_EQ(closed.Failure, state.Failure);
    EXPECT_TRUE(closed.Failed);
    EXPECT_FALSE(closed.CleanupPending);
    EXPECT_FALSE(closed.LeaseHeld);
}

#ifdef _WIN32
TEST_F(LogFiles, ExternalReaderKeepsDispositionDeletionPending) { VerifyDeletionWaitsForExternalHandle(GENERIC_READ); }

TEST_F(LogFiles, ExternalMetadataHandleKeepsDispositionDeletionPending)
{
    VerifyDeletionWaitsForExternalHandle(FILE_READ_ATTRIBUTES);
}

TEST_F(LogFiles, ExternalZeroAccessHandleKeepsDispositionDeletionPending) { VerifyDeletionWaitsForExternalHandle(0); }
TEST_F(LogFiles, PendingDeleteIsAccountedOnlyAfterCloseReleasesHandle)
{
    pg::log::detail::NativeIoFaults faults;
    FileStore store(&faults);
    ASSERT_TRUE(store.Open(Config, Report));
    const auto oldest = std::filesystem::path(store.Inspect().ActivePath.View());
    const std::string line(12288, 'x');
    ASSERT_TRUE(store.Write(line).Complete);
    ASSERT_TRUE(store.Write(line).Complete);
    faults.DeleteCloseFailure = pg::log::detail::NativeIoError(131);
    faults.DeleteCloseRetainsResource = true;
    EXPECT_FALSE(store.Write(line).Complete);
    EXPECT_TRUE(ContainsEntry(oldest));
    EXPECT_EQ(store.Inspect().ManagedBytes, 26624U);
    EXPECT_EQ(store.Inspect().ReclaimedBytes, 0U);
    EXPECT_EQ(store.Inspect().ReclaimedFiles, 0U);
    EXPECT_TRUE(store.Inspect().Failed);
    EXPECT_EQ(store.Inspect().Failure, faults.DeleteCloseFailure);
    const auto primary = store.Inspect().Failure;
    EXPECT_FALSE(store.Close());
    EXPECT_TRUE(ContainsEntry(oldest));
    EXPECT_TRUE(store.Inspect().CleanupPending);
    EXPECT_TRUE(store.Inspect().LeaseHeld);
    EXPECT_EQ(store.Inspect().ManagedBytes, 26624U);
    EXPECT_EQ(store.Inspect().ReclaimedFiles, 0U);
    FileStore competitor;
    EXPECT_FALSE(competitor.Open(Config, Report));
    // 这次关闭实际释放句柄，但仍报告错误；进度不能再被错误掩盖。
    faults.DeleteCloseRetainsResource = false;
    faults.DeleteCloseFailure = pg::log::detail::NativeIoError(132);
    EXPECT_FALSE(store.Close());
    EXPECT_FALSE(std::filesystem::exists(oldest));
    EXPECT_EQ(store.Inspect().ManagedBytes, 13312U);
    EXPECT_EQ(store.Inspect().ReclaimedBytes, 13312U);
    EXPECT_EQ(store.Inspect().ReclaimedFiles, 1U);
    EXPECT_FALSE(store.Inspect().CleanupPending);
    EXPECT_FALSE(store.Inspect().LeaseHeld);
    EXPECT_EQ(store.Inspect().Failure, primary);
    EXPECT_EQ(store.Inspect().CleanupFailure, faults.DeleteCloseFailure);
    faults.DeleteCloseFailure.reset();
    EXPECT_TRUE(store.Close());
    EXPECT_EQ(store.Inspect().ReclaimedBytes, 13312U);
    EXPECT_EQ(store.Inspect().ReclaimedFiles, 1U);
    EXPECT_FALSE(store.Write("still failed").Complete);
    FileStore successor;
    EXPECT_TRUE(successor.Open(Config, Report));
}
#endif

TEST_F(LogFiles, NoReopenAfterFailure)
{
    pg::log::detail::NativeIoFaults faults;
    FileStore store(&faults);
    ASSERT_TRUE(store.Open(Config, Report));
    faults.WriteLimit = 2;
    const auto partial = store.Write("abcdef");
    EXPECT_FALSE(partial.Complete);
    EXPECT_EQ(partial.BytesWritten, 2U);
    faults.WriteLimit = 100;
    EXPECT_FALSE(store.Write("abcdef").Complete);
    EXPECT_FALSE(store.Open(Config, Report));
    EXPECT_EQ(store.Inspect().ManagedBytes, 1026U);
}

TEST_F(LogFiles, CloseRetainsLeaseUntilOwnedResourcesReleased)
{
    pg::log::detail::NativeIoFaults faults;
    FileStore store(&faults);
    ASSERT_TRUE(store.Open(Config, Report));
    faults.CloseFailure = pg::log::detail::NativeIoError(125);
    faults.CloseRetainsResource = true;
    EXPECT_FALSE(store.Close());
    EXPECT_TRUE(store.Inspect().CleanupPending);
    EXPECT_TRUE(store.Inspect().LeaseHeld);
    FileStore competitor;
    EXPECT_FALSE(competitor.Open(Config, Report));
    faults.CloseFailure.reset();
    EXPECT_TRUE(store.Close());
    EXPECT_FALSE(store.Inspect().CleanupPending);
}

TEST_F(LogFiles, CreateAndFlushFailuresPreserveNativeReason)
{
    pg::log::detail::NativeIoFaults faults;
    FileStore store(&faults);
    ASSERT_TRUE(store.Open(Config, Report));
    faults.FlushFailure = pg::log::detail::NativeIoError(126);
    EXPECT_FALSE(store.Flush().Complete);
    const auto state = store.Inspect();
    ASSERT_TRUE(state.Failure);
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) 上方 ASSERT_TRUE 失败会退出当前 GTest 用例。
    const auto native = state.Failure->Native();
    ASSERT_TRUE(native);
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) 已通过 ASSERT_TRUE(native)。
    EXPECT_EQ(native->Value, 126);
    EXPECT_FALSE(store.Write("after flush").Complete);
}

TEST_F(LogFiles, InvalidDirectoryDoesNotAcquireLease)
{
    const auto missing = Directory / "missing";
    Config.Directory = missing.native();
    FileStore store;
    EXPECT_FALSE(store.Open(Config, Report));
    EXPECT_FALSE(store.Inspect().LeaseHeld);
    EXPECT_FALSE(std::filesystem::exists(missing));
}

TEST_F(LogFiles, ReadyInitializationDoesNotReportPendingRollback)
{
    FileStore store;
    ASSERT_TRUE(store.Open(Config, Report));
    EXPECT_FALSE(Report.CleanupPending);
    EXPECT_TRUE(Report.File.LeaseHeld);
}

TEST_F(LogFiles, InspectIntoOwnsPathAndUpdatesResourceFlags)
{
    pg::log::FileStoreSnapshot owned;
    {
        FileStore store;
        ASSERT_TRUE(store.Open(Config, Report));
        store.Inspect(owned);
        EXPECT_TRUE(owned.LeaseHeld);
        EXPECT_TRUE(owned.CleanupPending);
        EXPECT_EQ(owned.ActivePath.View(), Report.FilePath.View());
        ASSERT_TRUE(store.Close());
        pg::log::FileStoreSnapshot closed;
        store.Inspect(closed);
        EXPECT_FALSE(closed.LeaseHeld);
        EXPECT_FALSE(closed.CleanupPending);
        EXPECT_EQ(closed.ActivePath.View(), owned.ActivePath.View());
    }
    EXPECT_TRUE(owned.LeaseHeld);
    EXPECT_TRUE(owned.CleanupPending);
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(owned.ActivePath.View())));
}

TEST_F(LogFiles, CloseBeforeOpenIsTerminal)
{
    FileStore store;
    ASSERT_TRUE(store.Close());
    EXPECT_FALSE(store.Open(Config, Report));
    EXPECT_FALSE(store.Inspect().LeaseHeld);
}

TEST_F(LogFiles, CreateFailureAfterRotationStopsWithoutReopening)
{
    pg::log::detail::NativeIoFaults faults;
    FileStore store(&faults);
    ASSERT_TRUE(store.Open(Config, Report));
    ASSERT_TRUE(store.Write(std::string(12288, 'x')).Complete);
    faults.CreateFailure = pg::log::detail::NativeIoError(128);
    EXPECT_FALSE(store.Write("rotate").Complete);
    const auto state = store.Inspect();
    ASSERT_TRUE(state.Failure);
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) 上方 ASSERT_TRUE 失败会退出当前 GTest 用例。
    const auto native = state.Failure->Native();
    ASSERT_TRUE(native);
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) 已通过 ASSERT_TRUE(native)。
    EXPECT_EQ(native->Value, 128);
    faults.CreateFailure.reset();
    EXPECT_FALSE(store.Write("still failed").Complete);
    EXPECT_EQ(store.Inspect().ManagedBytes, 13312U);
}

TEST_F(LogFiles, OldestHistoryIsReclaimedFirst)
{
    FileStore store;
    ASSERT_TRUE(store.Open(Config, Report));
    const auto oldest = std::filesystem::path(store.Inspect().ActivePath.View());
    ASSERT_TRUE(store.Write(std::string(12288, 'a')).Complete);
    ASSERT_TRUE(store.Write(std::string(12288, 'b')).Complete);
    const auto newer = std::filesystem::path(store.Inspect().ActivePath.View());
    ASSERT_TRUE(store.Write(std::string(12288, 'c')).Complete);
    EXPECT_FALSE(std::filesystem::exists(oldest));
    EXPECT_TRUE(std::filesystem::exists(newer));
}

TEST_F(LogFiles, DamagedOwnershipAndManagedHardLinksArePreserved)
{
    std::filesystem::path damaged;
    {
        FileStore first;
        ASSERT_TRUE(first.Open(Config, Report));
        damaged = first.Inspect().ActivePath.View();
        ASSERT_TRUE(first.Write(std::string(12288, 'a')).Complete);
    }
    {
        // NOLINTNEXTLINE(bugprone-signed-bitwise) 标准库 openmode 位集合。
        std::fstream file(damaged, std::ios::in | std::ios::out | std::ios::binary);
        file.put('!');
    }
    std::filesystem::path linked;
    {
        FileStore second;
        ASSERT_TRUE(second.Open(Config, Report));
        linked = second.Inspect().ActivePath.View();
        ASSERT_TRUE(second.Write(std::string(12288, 'a')).Complete);
    }
    std::error_code error;
    std::filesystem::create_hard_link(linked, Directory / "external-link", error);
    ASSERT_FALSE(error);
    FileStore third;
    ASSERT_TRUE(third.Open(Config, Report));
    for (int index = 0; index < 4; ++index)
    {
        ASSERT_TRUE(third.Write(std::string(12288, 'b')).Complete);
    }
    EXPECT_TRUE(std::filesystem::exists(damaged));
    EXPECT_TRUE(std::filesystem::exists(linked));
    EXPECT_EQ(std::filesystem::file_size(linked), 13312U);
}

TEST_F(LogFiles, FileFailureDoesNotStopNativeConsole)
{
    pg::log::detail::NativeIoFaults faults;
    FileStore store(&faults);
    ASSERT_TRUE(store.Open(Config, Report));
    pg::log::detail::NativeLogIo console;
    ASSERT_TRUE(console.OpenConsole());
    faults.WriteFailure = pg::log::detail::NativeIoError(129);
    EXPECT_FALSE(store.Write("file failed\n").Complete);
    EXPECT_TRUE(console.Write("healthy console\n").Complete);
    EXPECT_TRUE(console.Flush().Complete);
}
} // namespace
