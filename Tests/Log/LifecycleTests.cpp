#include "ConsumerPause.h"
#include "LogConfig.h"
#include "LogReports.h"
#include "LogTypes.h"
#include "LogWriter.h"
#include <Log.h>
#include <LogSystem.h>
#include <LogTestAccess.h>

#include <atomic>
#include <gtest/gtest.h>

#include <chrono>

namespace pg::log
{
TEST(LogLifecycle, WriterAfterHostIndependentOwnerDeath)
{
    LogWriter stale;
    {
        LogSystem system;
        LogConfig config;
        config.Enabled = OutputMask::kConsole;
        LogInitReport initialized;
        ASSERT_TRUE(system.Initialize(config, initialized));
        stale = system.GetWriter();
        ASSERT_EQ(test::SubmitPaused(system, stale, [&](const auto& pausedWriter)
                                     { return PG_LOG_INFO(pausedWriter, "lifecycle", "owned data {}", 42); }),
                  SubmitResult::kAccepted);
        StopReport stopped;
        ASSERT_TRUE(system.Stop(std::chrono::steady_clock::now() + std::chrono::seconds(2), stopped));
        EXPECT_TRUE(stopped.ResourcesReleased);
        EXPECT_TRUE(stopped.FinalFlush.OutputComplete);
        EXPECT_EQ(stopped.FinalFlush.H, 1);
        EXPECT_EQ(stopped.FinalFlush.Outputs.at(0).Delivered, 1);
        StopReport again;
        ASSERT_TRUE(system.Stop(std::chrono::steady_clock::now(), again));
        EXPECT_EQ(again.FinalFlush.H, 1);
    }
    auto copy = stale;
    EXPECT_EQ(PG_LOG_ERROR(copy, "stale", "closed"), SubmitResult::kClosed);
}

TEST(LogLifecycle, ColdStopCachesZeroRange)
{
    LogSystem system;
    StopReport report;
    ASSERT_TRUE(system.Stop(std::chrono::steady_clock::now(), report));
    EXPECT_EQ(system.GetState(), LogState::kClosed);
    EXPECT_TRUE(report.ResourcesReleased);
    EXPECT_TRUE(report.FinalFlush.Finished);
    EXPECT_TRUE(report.FinalFlush.OutputComplete);
    EXPECT_EQ(report.FinalFlush.H, 0);
}

TEST(LogLifecycle, RuntimeFailureStopsOneOutputAndKeepsItsFirstError)
{
    detail::LogTestHooks hooks;
    const auto failure = MakeLogError(LogError::kIoFailed);
    hooks.Console.WriteFailure = failure;
    LogSystem system;
    detail::LogTestAccess::Attach(system, hooks);
    LogConfig config;
    config.Enabled = OutputMask::kConsole;
    LogInitReport initialized;
    ASSERT_TRUE(system.Initialize(config, initialized));
    const auto writer = system.GetWriter();
    ASSERT_EQ(test::SubmitPaused(system, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_ERROR(pausedWriter, "failure", "first"); }),
              SubmitResult::kAccepted);
    ASSERT_EQ(test::SubmitPaused(system, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_ERROR(pausedWriter, "failure", "second"); }),
              SubmitResult::kAccepted);
    StopReport stopped;
    ASSERT_TRUE(system.Stop(std::chrono::steady_clock::now() + std::chrono::seconds(2), stopped));
    EXPECT_TRUE(stopped.ResourcesReleased);
    EXPECT_FALSE(stopped.FinalFlush.OutputComplete);
    EXPECT_EQ(stopped.FinalFlush.Outputs.at(0).Failure, failure);
    EXPECT_EQ(stopped.FinalFlush.Outputs.at(0).Delivered, 0);
    EXPECT_EQ(stopped.FinalFlush.Outputs.at(0).Undelivered, 2);
    EXPECT_EQ(stopped.FinalFlush.Failed, OutputMask::kConsole);
}

TEST(LogLifecycle, RetainedOutputCloseKeepsEveryOwnerUntilRetry)
{
    detail::LogTestHooks hooks;
    hooks.Console.CloseFailure = MakeLogError(LogError::kIoFailed);
    hooks.Console.CloseRetainsResource = true;
    LogSystem system;
    detail::LogTestAccess::Attach(system, hooks);
    LogConfig config;
    config.Enabled = OutputMask::kConsole;
    LogInitReport initialized;
    ASSERT_TRUE(system.Initialize(config, initialized));
    StopReport stopped;
    EXPECT_FALSE(system.Stop(std::chrono::steady_clock::now() + std::chrono::milliseconds(50), stopped));
    EXPECT_GT(hooks.CleanupAttempts.load(std::memory_order_acquire), 0);
    EXPECT_EQ(system.GetState(), LogState::kClosing);
    EXPECT_EQ(hooks.DataBlocks.load(), 1);
    EXPECT_EQ(hooks.ControlOwners.load(), 1);
    EXPECT_EQ(hooks.WorkerHandles.load(), 1);
    EXPECT_FALSE(stopped.ResourcesReleased);
    hooks.Console.CloseFailure.reset();
    hooks.Console.CloseRetainsResource = false;
    ASSERT_TRUE(system.Stop(std::chrono::steady_clock::now() + std::chrono::seconds(2), stopped));
    EXPECT_TRUE(stopped.ResourcesReleased);
    EXPECT_FALSE(stopped.FinalFlush.OutputComplete);
    EXPECT_EQ(hooks.DataBlocks.load(), 0);
    EXPECT_EQ(hooks.ControlOwners.load(), 0);
    EXPECT_EQ(hooks.WorkerHandles.load(), 0);
}
} // namespace pg::log
