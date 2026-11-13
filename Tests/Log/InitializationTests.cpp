#include "ConsumerPause.h"
#include "LogConfig.h"
#include "LogRecord.h"
#include "LogReports.h"
#include "LogTypes.h"
#include <Log.h>
#include <LogSystem.h>
#include <LogTestAccess.h>

#include <cstddef>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <string>
#include <type_traits>

namespace pg::log
{
namespace
{
LogConfig ConsoleConfig()
{
    LogConfig config;
    config.Enabled = OutputMask::kConsole;
    return config;
}
} // namespace

TEST(LogInit, DisabledNeedsNoHeapOrThread)
{
    detail::LogTestHooks hooks;
    hooks.FailAt = detail::LogInitStage::kDataAllocation;
    LogSystem system;
    detail::LogTestAccess::Attach(system, hooks);
    EXPECT_EQ(system.GetState(), LogState::kCold);
    auto config = ConsoleConfig();
    config.Enabled = OutputMask::kNone;
    LogInitReport report;
    ASSERT_TRUE(system.Initialize(config, report));
    EXPECT_EQ(report.Disposition, LogInitDisposition::kDisabled);
    EXPECT_EQ(hooks.DataBlocks.load(), 0);
    EXPECT_EQ(hooks.ControlOwners.load(), 0);
    EXPECT_EQ(hooks.WorkerHandles.load(), 0);
    EXPECT_EQ(PG_LOG_INFO(system.GetWriter(), "disabled", "unused"), SubmitResult::kDisabled);
    StopReport stopped;
    ASSERT_TRUE(system.Stop(std::chrono::steady_clock::now(), stopped));
    EXPECT_TRUE(stopped.ResourcesReleased);
    EXPECT_TRUE(stopped.FinalFlush.Finished);
    EXPECT_EQ(stopped.FinalFlush.H, 0);
}

TEST(LogInit, EveryFailurePointRetainsCleanupOwner)
{
    constexpr std::array kStages{
        detail::LogInitStage::kConfigCopy,      detail::LogInitStage::kDataAllocation,
        detail::LogInitStage::kQueue,           detail::LogInitStage::kControlAllocation,
        detail::LogInitStage::kSynchronization, detail::LogInitStage::kConsole,
        detail::LogInitStage::kBackend,         detail::LogInitStage::kThread,
    };
    for (const auto stage : kStages)
    {
        SCOPED_TRACE(static_cast<unsigned int>(stage));
        detail::LogTestHooks hooks;
        hooks.FailAt = stage;
        LogSystem system;
        detail::LogTestAccess::Attach(system, hooks);
        auto config = ConsoleConfig();
        config.Required = OutputMask::kConsole;
        LogInitReport report;
        const auto result = system.Initialize(config, report);
        ASSERT_FALSE(result);
        ASSERT_TRUE(report.Failure);
        // GTest 的 ASSERT_TRUE 已保证有值，clang-tidy 不识别宏展开后的终止分支。
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        EXPECT_EQ(*report.Failure, result.error());
        EXPECT_EQ(system.GetState(), LogState::kClosed);
        EXPECT_FALSE(report.CleanupPending);
        EXPECT_EQ(hooks.DataBlocks.load(), 0);
        EXPECT_EQ(hooks.ControlOwners.load(), 0);
        EXPECT_EQ(hooks.WorkerHandles.load(), 0);
        StopReport stopped;
        ASSERT_TRUE(system.Stop(std::chrono::steady_clock::now(), stopped));
        EXPECT_TRUE(stopped.ResourcesReleased);
        EXPECT_FALSE(stopped.FinalFlush.OutputComplete);
        EXPECT_EQ(stopped.FinalFlush.InitializationFailure, report.Failure);
    }
}

TEST(LogInit, RequiredFailureNeverPublishesWriter)
{
    LogSystem system;
    auto config = ConsoleConfig();
    config.Required = OutputMask::kFile;
    LogInitReport report;
    ASSERT_FALSE(system.Initialize(config, report));
    EXPECT_EQ(report.Failure, MakeLogError(LogError::kInvalidConfig));
    EXPECT_NE(system.GetState(), LogState::kRunning);
    EXPECT_EQ(PG_LOG_INFO(system.GetWriter(), "failed", "unpublished"), SubmitResult::kDisabled);
}

TEST(LogInit, OptionalFileFailureKeepsConsole)
{
    LogSystem system;
    const LogConfig config;
    LogInitReport report;
    ASSERT_TRUE(system.Initialize(config, report));
    EXPECT_EQ(report.Disposition, LogInitDisposition::kDegraded);
    EXPECT_EQ(report.Ready, OutputMask::kConsole);
    EXPECT_EQ(report.Failed, OutputMask::kFile);
    EXPECT_FALSE(report.CleanupPending);
    EXPECT_EQ(test::SubmitPaused(system, system.GetWriter(), [&](const auto& pausedWriter)
                                 { return PG_LOG_ERROR(pausedWriter, "init", "healthy console"); }),
              SubmitResult::kAccepted);
    StopReport stopped;
    ASSERT_TRUE(system.Stop(std::chrono::steady_clock::now() + std::chrono::seconds(2), stopped));
    EXPECT_EQ(stopped.FinalFlush.H, 1);
    EXPECT_EQ(stopped.FinalFlush.Outputs.at(0).Delivered, 1);
    EXPECT_EQ(stopped.FinalFlush.Outputs.at(1).Undelivered, 1);
    EXPECT_FALSE(stopped.FinalFlush.OutputComplete);
}

TEST(LogInit, BothUnavailableIsNotRunning)
{
    detail::LogTestHooks hooks;
    hooks.FailAt = detail::LogInitStage::kConsole;
    LogSystem system;
    detail::LogTestAccess::Attach(system, hooks);
    LogInitReport report;
    ASSERT_FALSE(system.Initialize(LogConfig{}, report));
    EXPECT_EQ(report.Failure, MakeLogError(LogError::kNoOutputs));
    EXPECT_EQ(report.Failed, OutputMask::kConsole | OutputMask::kFile);
    EXPECT_EQ(system.GetState(), LogState::kClosed);
}

TEST(LogInit, FailedCleanupKeepsOwnerAndOriginalError)
{
    detail::LogTestHooks hooks;
    hooks.FailAt = detail::LogInitStage::kThread;
    hooks.Console.CloseFailure = MakeLogError(LogError::kIoFailed);
    hooks.Console.CloseRetainsResource = true;
    LogSystem system;
    detail::LogTestAccess::Attach(system, hooks);
    LogInitReport report;
    ASSERT_FALSE(system.Initialize(ConsoleConfig(), report));
    EXPECT_EQ(report.Failure, MakeLogError(LogError::kThreadStartFailed));
    EXPECT_TRUE(report.CleanupPending);
    EXPECT_EQ(system.GetState(), LogState::kClosing);
    EXPECT_EQ(hooks.DataBlocks.load(), 1);
    hooks.Console.CloseFailure.reset();
    hooks.Console.CloseRetainsResource = false;
    StopReport stopped;
    ASSERT_TRUE(system.Stop(std::chrono::steady_clock::now() + std::chrono::seconds(2), stopped));
    EXPECT_TRUE(stopped.ResourcesReleased);
    EXPECT_EQ(stopped.FinalFlush.InitializationFailure, report.Failure);
    EXPECT_FALSE(stopped.FinalFlush.OutputComplete);
    EXPECT_EQ(hooks.DataBlocks.load(), 0);
}

TEST(LogInit, RealLayoutBudgetRejectsBeforeAllocation)
{
    static_assert(!std::is_move_constructible_v<LogSystem>);
    auto config = ConsoleConfig();
    const auto memory = detail::LogTestAccess::MemoryUsage(config);
    EXPECT_GT(memory.DataBytes, 256 * sizeof(LogRecord));
    EXPECT_LE(memory.RequiredBytes, 1024UZ * 1024);
    EXPECT_GT(memory.RequiredBytes, (256 * sizeof(LogRecord)) + (256UZ * 1024));
    config.MemoryBudgetBytes = memory.RequiredBytes - 1;
    detail::LogTestHooks hooks;
    LogSystem system;
    detail::LogTestAccess::Attach(system, hooks);
    LogInitReport report;
    ASSERT_FALSE(system.Initialize(config, report));
    EXPECT_EQ(report.Failure, MakeLogError(LogError::kInvalidConfig));
    EXPECT_EQ(hooks.DataBlocks.load(), 0);
}

TEST(LogInit, WorkerStackTooSmallFailsBeforeAllocation)
{
    for (const auto stack : std::array{1UZ, 16384UZ, 65536UZ, 131071UZ})
    {
        auto config = ConsoleConfig();
        config.WorkerStackBytes = stack;
        detail::LogTestHooks hooks;
        LogSystem system;
        detail::LogTestAccess::Attach(system, hooks);
        LogInitReport report;
        EXPECT_FALSE(system.Initialize(config, report));
        EXPECT_EQ(report.Failure, MakeLogError(LogError::kInvalidConfig));
        EXPECT_EQ(hooks.DataBlocks.load(), 0);
    }
}

TEST(LogInit, RequiredFileFailureReturnsItsOriginalError)
{
    const auto directory = std::filesystem::path{PG_LOG_INIT_TEST_ROOT} / "required-file";
    std::filesystem::create_directories(directory);
    auto config = ConsoleConfig();
    config.Enabled = OutputMask::kConsole | OutputMask::kFile;
    config.Required = OutputMask::kFile;
    const auto& path = directory.native();
    config.Directory = path;
    config.PartitionId.front() = std::byte{1};
    detail::LogTestHooks hooks;
    hooks.FailAt = detail::LogInitStage::kFile;
    LogSystem system;
    detail::LogTestAccess::Attach(system, hooks);
    LogInitReport report;
    ASSERT_FALSE(system.Initialize(config, report));
    EXPECT_EQ(report.Failure, MakeLogError(LogError::kFileOpenFailed));
    EXPECT_EQ(system.GetState(), LogState::kClosed);
    EXPECT_EQ(hooks.DataBlocks.load(), 0);
    EXPECT_FALSE(report.CleanupPending);
}

TEST(LogInit, RealConsumerWritesBothOutputsAndOwnsItsPath)
{
    const auto directory = std::filesystem::path{PG_LOG_INIT_TEST_ROOT} / "two-outputs";
    std::filesystem::create_directories(directory);
    LogConfig config;
    auto path = directory.native();
    config.Directory = path;
    config.PartitionId.front() = std::byte{2};
    config.Required = config.Enabled;
    LogSystem system;
    LogInitReport report;
    ASSERT_TRUE(system.Initialize(config, report));
    EXPECT_EQ(report.Disposition, LogInitDisposition::kReady);
    EXPECT_TRUE(report.File.LeaseHeld);
    EXPECT_TRUE(report.File.CleanupPending);
    EXPECT_FALSE(report.CleanupPending);
    const auto savedPath = report.FilePath;
    path.assign(path.size(), static_cast<std::filesystem::path::value_type>('x'));
    ASSERT_EQ(test::SubmitPaused(system, system.GetWriter(), [&](const auto& pausedWriter)
                                 { return PG_LOG_ERROR(pausedWriter, "both", "independent owned message"); }),
              SubmitResult::kAccepted);
    StopReport stopped;
    ASSERT_TRUE(system.Stop(std::chrono::steady_clock::now() + std::chrono::seconds(2), stopped));
    EXPECT_TRUE(stopped.ResourcesReleased);
    EXPECT_TRUE(stopped.FinalFlush.OutputComplete);
    EXPECT_EQ(stopped.FinalFlush.Outputs.at(0).Delivered, 1);
    EXPECT_EQ(stopped.FinalFlush.Outputs.at(1).Delivered, 1);
    EXPECT_GT(std::filesystem::file_size(std::filesystem::path{savedPath.View()}), 1024);
    const auto snapshot = system.Inspect();
    EXPECT_FALSE(snapshot.File.CleanupPending);
    EXPECT_EQ(snapshot.HighestAccepted, 1);
    EXPECT_EQ(snapshot.HighestProcessed, 1);
}

TEST(LogInit, RequiredFileMayFailAtRuntimeWhileConsoleContinues)
{
    const auto directory = std::filesystem::path{PG_LOG_INIT_TEST_ROOT} / "runtime-file";
    std::filesystem::create_directories(directory);
    LogConfig config;
    config.Directory = directory.native();
    config.PartitionId.front() = std::byte{3};
    config.Required = config.Enabled;
    detail::LogTestHooks hooks;
    LogSystem system;
    detail::LogTestAccess::Attach(system, hooks);
    LogInitReport report;
    ASSERT_TRUE(system.Initialize(config, report));
    // 消费者尚无记录；首个入队的 release/acquire 将故障值交给消费者。
    hooks.File.WriteFailure = MakeLogError(LogError::kIoFailed);
    const auto writer = system.GetWriter();
    EXPECT_EQ(test::SubmitPaused(system, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_ERROR(pausedWriter, "runtime", "first"); }),
              SubmitResult::kAccepted);
    EXPECT_EQ(test::SubmitPaused(system, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_ERROR(pausedWriter, "runtime", "second"); }),
              SubmitResult::kAccepted);
    StopReport stopped;
    ASSERT_TRUE(system.Stop(std::chrono::steady_clock::now() + std::chrono::seconds(2), stopped));
    EXPECT_TRUE(stopped.ResourcesReleased);
    EXPECT_FALSE(stopped.FinalFlush.OutputComplete);
    EXPECT_EQ(stopped.FinalFlush.Outputs.at(0).Delivered, 2);
    EXPECT_EQ(stopped.FinalFlush.Outputs.at(1).Undelivered, 2);
    EXPECT_EQ(stopped.FinalFlush.Failed, OutputMask::kFile);
    EXPECT_FALSE(system.Inspect().File.LeaseHeld);
}
} // namespace pg::log
