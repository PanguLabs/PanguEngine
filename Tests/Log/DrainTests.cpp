#include "ConsumerPause.h"
#include "DrainTestSupport.h"

#include <Log.h>
#include <LogConfig.h>
#include <LogReports.h>
#include <LogTestAccess.h>
#include <LogTypes.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

namespace pg::log
{
TEST(LogDrain, EmptyConsumerContentionDoesNotAdvanceAcceptedPrefix)
{
    test::DrainOwner owner;
    ASSERT_TRUE(owner.Start());
    const auto writer = owner.System.GetWriter();
    owner.Hooks.BlockQueuePop.store(true);
    ASSERT_TRUE(test::Wait([&] { return owner.Hooks.QueuePopEntered.load(); }));
    // 真实空 TryPop 持锁时首条也合法拒收，不能给固定 H 准备假定接纳。
    EXPECT_EQ(PG_LOG_INFO(writer, "drain", "contended"), SubmitResult::kDropped);
    owner.Hooks.BlockQueuePop.store(false);
    {
        const test::ConsumerPause pause{owner.System};
        ASSERT_TRUE(pause.Owns());
        EXPECT_EQ(PG_LOG_INFO(writer, "drain", "controlled"), SubmitResult::kAccepted);
    }
    FlushReport report;
    ASSERT_TRUE(owner.System.Flush(test::Deadline(), report));
    EXPECT_EQ(report.H, 1);
    EXPECT_EQ(report.Outputs.at(0).Delivered, 1);
    EXPECT_EQ(report.Rejected.at(2).Contention, 1);
    EXPECT_FALSE(report.OutputComplete);
}
TEST(LogDrain, CapturesAlreadyProcessedPrefix)
{
    test::DrainOwner owner;
    ASSERT_TRUE(owner.Start());
    EXPECT_EQ(test::SubmitPaused(owner.System, owner.System.GetWriter(),
                                 [&](const auto& pausedWriter) { return PG_LOG_INFO(pausedWriter, "drain", "first"); }),
              SubmitResult::kAccepted);
    ASSERT_TRUE(test::Wait([&] { return owner.System.Inspect().HighestProcessed == 1; }));
    FlushReport report;
    ASSERT_TRUE(test::Flush(owner.System, test::Deadline(), report));
    EXPECT_EQ(report.H, 1);
    EXPECT_EQ(report.Outputs.at(0).Delivered, 1);
    EXPECT_TRUE(report.OutputComplete);
}
TEST(LogDrain, EmptyAndFilteredPrefixIsComplete)
{
    test::DrainOwner owner;
    ASSERT_TRUE(owner.Start());
    EXPECT_EQ(PG_LOG_DEBUG(owner.System.GetWriter(), "drain", "filtered"), SubmitResult::kFiltered);
    FlushReport report;
    ASSERT_TRUE(test::Flush(owner.System, test::Deadline(), report));
    EXPECT_EQ(report.H, 0);
    EXPECT_TRUE(report.OutputComplete);
}
TEST(LogDrain, CompletedWithRejectedRecordsIsIncomplete)
{
    test::DrainOwner owner;
    ASSERT_TRUE(owner.Start(1));
    owner.Hooks.BlockOutput.store(true);
    const auto writer = owner.System.GetWriter();
    ASSERT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "drain", "in flight"); }),
              SubmitResult::kAccepted);
    ASSERT_TRUE(test::Wait([&] { return owner.Hooks.OutputEntered.load(); }));
    ASSERT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "drain", "queued"); }),
              SubmitResult::kAccepted);
    EXPECT_EQ(PG_LOG_INFO(writer, "drain", "rejected"), SubmitResult::kDropped);
    owner.Hooks.BlockOutput.store(false);
    FlushReport report;
    ASSERT_TRUE(test::Flush(owner.System, test::Deadline(), report));
    EXPECT_TRUE(report.Finished);
    EXPECT_EQ(report.H, 2);
    EXPECT_EQ(report.Rejected.at(2).Capacity, 1);
    EXPECT_FALSE(report.OutputComplete);
}
TEST(LogDrain, AcceptedTruncatedAndConstructionFailureAreCumulative)
{
    test::DrainOwner owner;
    ASSERT_TRUE(owner.Start());
    const auto writer = owner.System.GetWriter();
    EXPECT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "drain", "{}", std::string(1100, 'x')); }),
              SubmitResult::kAccepted);
    EXPECT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "drain", "{:08}", 1); }),
              SubmitResult::kAccepted);
    FlushReport report;
    ASSERT_TRUE(test::Flush(owner.System, test::Deadline(), report));
    EXPECT_EQ(report.Truncated, 1);
    EXPECT_EQ(report.ConstructionFailed, 1);
    EXPECT_FALSE(report.OutputComplete);
    FlushReport repeated;
    ASSERT_TRUE(test::Flush(owner.System, test::Deadline(), repeated));
    EXPECT_EQ(repeated.Truncated, 1);
    EXPECT_EQ(repeated.ConstructionFailed, 1);
}
TEST(LogDrain, OverflowNeverReportsComplete)
{
    test::DrainOwner owner;
    ASSERT_TRUE(owner.Start());
    detail::LogTestAccess::MarkCountersUntrusted(owner.System);
    FlushReport report;
    ASSERT_TRUE(test::Flush(owner.System, test::Deadline(), report));
    EXPECT_TRUE(report.Finished);
    EXPECT_TRUE(report.CounterOverflow);
    EXPECT_FALSE(report.OutputComplete);
}
TEST(LogDrain, QueueAndWriterRejectionSumCannotWrap)
{
    test::DrainOwner owner;
    ASSERT_TRUE(owner.Start());
    detail::LogTestAccess::SeedRejectionSum(owner.System);
    FlushReport report;
    ASSERT_TRUE(test::Flush(owner.System, test::Deadline(), report));
    EXPECT_EQ(report.Rejected.at(2).Contention, std::numeric_limits<std::uint64_t>::max());
    EXPECT_TRUE(report.CounterOverflow);
    EXPECT_FALSE(report.OutputComplete);
}
TEST(LogDrain, ControlNoticeDoesNotAdvanceHOrRepeat)
{
    test::DrainOwner owner;
    const LogConfig config;
    LogInitReport initialized;
    // 缺失目录使可选 File 初始化失败，Console 是唯一健康摘要接收方。
    ASSERT_TRUE(owner.System.Initialize(config, initialized));
    EXPECT_EQ(test::SubmitPaused(owner.System, owner.System.GetWriter(),
                                 [&](const auto& pausedWriter) { return PG_LOG_INFO(pausedWriter, "drain", "first"); }),
              SubmitResult::kAccepted);
    FlushReport report;
    ASSERT_TRUE(test::Flush(owner.System, test::Deadline(), report));
    EXPECT_EQ(report.H, 1);
    EXPECT_EQ(report.Outputs.at(0).Delivered, 1);
    EXPECT_EQ(report.Outputs.at(1).Undelivered, 1);
    EXPECT_FALSE(report.OutputComplete);
    EXPECT_TRUE(report.ControlNoticeAttempted);
    EXPECT_EQ(report.ControlOutputs.at(0).Delivered, 1);
    EXPECT_EQ(report.ControlOutputs.at(1).Delivered, 0);
    EXPECT_GT(report.ControlOutputs.at(0).BytesWritten, 0);
    EXPECT_EQ(test::SubmitPaused(owner.System, owner.System.GetWriter(), [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "drain", "second"); }),
              SubmitResult::kAccepted);
    StopReport stopped;
    ASSERT_TRUE(owner.System.Stop(test::Deadline(), stopped));
    EXPECT_EQ(stopped.FinalFlush.H, 2);
    EXPECT_EQ(stopped.FinalFlush.Outputs.at(0).Delivered, 2);
    EXPECT_EQ(stopped.FinalFlush.ControlOutputs.at(0).Delivered, 1);
}
TEST(LogDrain, ControlNoticeAfterHDoesNotPolluteFrozenCounterWindow)
{
    test::DrainOwner owner;
    const LogConfig config;
    LogInitReport initialized;
    ASSERT_TRUE(owner.System.Initialize(config, initialized));
    owner.Hooks.BlockSequence.store(1);
    ASSERT_EQ(test::SubmitPaused(owner.System, owner.System.GetWriter(), [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "drain", "prefix"); }),
              SubmitResult::kAccepted);
    ASSERT_TRUE(test::Wait([&] { return owner.Hooks.SequenceEntered.load(); }));
    FlushReport prefix;
    EXPECT_FALSE(owner.System.Flush(std::chrono::steady_clock::now(), prefix));
    ASSERT_EQ(prefix.H, 1U);
    EXPECT_FALSE(prefix.CounterOverflow);
    // H 已固定且消费者静止；新一轮生产者统计不可信不得改变该请求的采集窗口。
    detail::LogTestAccess::SeedRejectionSum(owner.System);
    owner.Hooks.BlockSequence.store(0);
    ASSERT_TRUE(owner.System.Flush(test::Deadline(), prefix));
    EXPECT_TRUE(prefix.Finished);
    EXPECT_TRUE(prefix.ControlNoticeAttempted);
    EXPECT_GT(prefix.ControlOutputs.at(0).Delivered, 0U);
    EXPECT_FALSE(prefix.CounterOverflow);
    EXPECT_EQ(prefix.Rejected.at(2).Contention, 0U);
    EXPECT_TRUE(owner.System.Inspect().CounterOverflow);
}
TEST(LogDrain, PartialWriteAndFlushFailureHaveSeparateLedgers)
{
    for (const bool partial : {false, true})
    {
        test::DrainOwner owner;
        if (partial)
        {
            owner.Hooks.Console.WriteLimit = 7;
        }
        else
        {
            owner.Hooks.Console.FlushFailure = MakeLogError(LogError::kIoFailed);
        }
        ASSERT_TRUE(owner.Start());
        ASSERT_EQ(test::SubmitPaused(owner.System, owner.System.GetWriter(), [&](const auto& pausedWriter)
                                     { return PG_LOG_INFO(pausedWriter, "drain", "record"); }),
                  SubmitResult::kAccepted);
        FlushReport report;
        ASSERT_TRUE(test::Flush(owner.System, test::Deadline(), report));
        EXPECT_TRUE(report.Finished);
        EXPECT_FALSE(report.OutputComplete);
        EXPECT_EQ(report.Outputs.at(0).Delivered, partial ? 0 : 1);
        EXPECT_EQ(report.Outputs.at(0).Undelivered, partial ? 1 : 0);
        EXPECT_EQ(report.Outputs.at(0).FlushSucceeded, false);
        if (partial)
        {
            EXPECT_EQ(report.Outputs.at(0).BytesWritten, 7);
        }
    }
}
TEST(LogDrain, DisabledAndFailedInitializationKeepZeroRangeStop)
{
    for (const bool disabled : {false, true})
    {
        test::DrainOwner owner;
        LogConfig config;
        config.Enabled = disabled ? OutputMask::kNone : OutputMask::kConsole;
        owner.Hooks.FailAt = detail::LogInitStage::kDataAllocation;
        LogInitReport init;
        const auto initialized = owner.System.Initialize(config, init);
        EXPECT_EQ(initialized.has_value(), disabled);
        StopReport stopped;
        ASSERT_TRUE(owner.System.Stop(std::chrono::steady_clock::now(), stopped));
        EXPECT_EQ(stopped.FinalFlush.H, 0);
        EXPECT_TRUE(stopped.ResourcesReleased);
        EXPECT_EQ(stopped.FinalFlush.OutputComplete, disabled);
        EXPECT_EQ(stopped.FinalFlush.InitializationFailure, init.Failure);
        FlushReport flush;
        const auto result = owner.System.Flush(test::Deadline(), flush);
        EXPECT_FALSE(result);
        EXPECT_EQ(result.error(), MakeLogError(LogError::kInvalidState));
    }
}
TEST(LogDrain, RejectedRecordFlagsDoNotEnterAcceptedCumulativeCounts)
{
    test::DrainOwner owner;
    ASSERT_TRUE(owner.Start(1));
    owner.Hooks.BlockOutput.store(true);
    const auto writer = owner.System.GetWriter();
    ASSERT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "drain", "in flight"); }),
              SubmitResult::kAccepted);
    ASSERT_TRUE(test::Wait([&] { return owner.Hooks.OutputEntered.load(); }));
    ASSERT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "drain", "queued"); }),
              SubmitResult::kAccepted);
    EXPECT_EQ(PG_LOG_INFO(writer, "drain", "{}", std::string(1100, 'x')), SubmitResult::kDropped);
    EXPECT_EQ(PG_LOG_INFO(writer, "drain", "{:8}", 1), SubmitResult::kDropped);
    owner.Hooks.BlockOutput.store(false);
    FlushReport report;
    ASSERT_TRUE(owner.System.Flush(test::Deadline(), report));
    EXPECT_EQ(report.H, 2);
    EXPECT_EQ(report.Truncated, 0);
    EXPECT_EQ(report.ConstructionFailed, 0);
    EXPECT_EQ(report.Rejected.at(2).Capacity, 2);
}
TEST(LogDrain, ConsumerReentryDoesNotSubmitOrWaitForItself)
{
    test::DrainOwner owner;
    ASSERT_TRUE(owner.Start());
    owner.Hooks.ProbeReentry.store(true);
    ASSERT_EQ(test::SubmitPaused(owner.System, owner.System.GetWriter(),
                                 [&](const auto& pausedWriter) { return PG_LOG_INFO(pausedWriter, "drain", "outer"); }),
              SubmitResult::kAccepted);
    FlushReport report;
    ASSERT_TRUE(owner.System.Flush(test::Deadline(), report));
    EXPECT_EQ(report.H, 1);
    EXPECT_EQ(owner.Hooks.ReentrySubmit.load(), SubmitResult::kDropped);
    EXPECT_TRUE(owner.Hooks.ReentryFlushBusy.load());
    EXPECT_TRUE(owner.Hooks.ReentryStopBusy.load());
    EXPECT_EQ(owner.System.Inspect().Rejected.at(2).Contention, 1);
}
TEST(LogDrain, NormalHistoryReclamationDoesNotInvalidateDeliveredPrefix)
{
    test::DrainOwner owner;
    const auto directory = std::filesystem::path{PG_LOG_INIT_TEST_ROOT} / "drain-reclamation";
    std::filesystem::create_directories(directory);
    LogConfig config;
    config.Enabled = OutputMask::kFile;
    config.Required = config.Enabled;
    config.Directory = directory.native();
    config.PartitionId.front() = std::byte{8};
    config.FileBytes = 13312;
    config.PartitionBytes = 26624;
    LogInitReport init;
    ASSERT_TRUE(owner.System.Initialize(config, init));
    const auto writer = owner.System.GetWriter();
    owner.Hooks.BlockOutput.store(true);
    const std::string message(1024, 'r');
    ASSERT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "reclaim", "{}", message); }),
              SubmitResult::kAccepted);
    ASSERT_TRUE(test::Wait([&] { return owner.Hooks.OutputEntered.load(); }));
    for (int index = 1; index < 80; ++index)
    {
        ASSERT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                     { return PG_LOG_INFO(pausedWriter, "reclaim", "{}", message); }),
                  SubmitResult::kAccepted);
    }
    owner.Hooks.BlockOutput.store(false);
    FlushReport report;
    ASSERT_TRUE(owner.System.Flush(test::Deadline(), report));
    EXPECT_EQ(report.H, 80);
    EXPECT_EQ(report.Outputs.at(1).Delivered, 80);
    EXPECT_TRUE(report.OutputComplete);
    EXPECT_GT(owner.System.Inspect().File.ReclaimedFiles, 0);
    // Flush 不回收文件句柄；退出前显式 Stop，不能依赖析构默认的短等待。
    StopReport stopped;
    ASSERT_TRUE(owner.System.Stop(test::Deadline(), stopped));
    EXPECT_TRUE(stopped.ResourcesReleased);
    EXPECT_EQ(stopped.FinalFlush.H, report.H);
    EXPECT_EQ(stopped.FinalFlush.Outputs.at(1).Delivered, 80);
    EXPECT_TRUE(stopped.FinalFlush.OutputComplete);
}
TEST(LogDrain, PendingStopReportsCleanupFailureWithoutLosingOwner)
{
    test::DrainOwner owner;
    owner.Hooks.Console.CloseFailure = MakeLogError(LogError::kIoFailed);
    owner.Hooks.Console.CloseRetainsResource = true;
    ASSERT_TRUE(owner.Start());
    StopReport first;
    EXPECT_FALSE(owner.System.Stop(std::chrono::steady_clock::now() + std::chrono::milliseconds(10), first));
    ASSERT_TRUE(test::Wait(
        [&]
        {
            return owner.Hooks.CleanupAttempts.load() > 0 &&
                   owner.System.Inspect().Outputs.at(0).CleanupFailure.has_value();
        }));
    const auto attempts = owner.Hooks.CleanupAttempts.load();
    StopReport pending;
    EXPECT_FALSE(owner.System.Stop(std::chrono::steady_clock::now(), pending));
    const bool cleanupRetried = test::Wait([&] { return owner.Hooks.CleanupAttempts.load() > attempts; });
    // 此次 Close 已返回且没有新 RetryCleanup，才能修改借用故障值。
    owner.Hooks.Console.CloseFailure.reset();
    owner.Hooks.Console.CloseRetainsResource = false;
    StopReport stopped;
    ASSERT_TRUE(owner.System.Stop(test::Deadline(), stopped));
    EXPECT_TRUE(cleanupRetried);
    EXPECT_FALSE(pending.ResourcesReleased);
    EXPECT_TRUE(pending.FinalFlush.Outputs.at(0).CleanupFailure.has_value());
    EXPECT_FALSE(pending.FinalFlush.OutputComplete);
    EXPECT_TRUE(stopped.ResourcesReleased);
    EXPECT_TRUE(stopped.FinalFlush.Outputs.at(0).CleanupFailure.has_value());
    EXPECT_FALSE(stopped.FinalFlush.OutputComplete);
}
} // namespace pg::log
