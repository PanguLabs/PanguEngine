#include "ConsumerPause.h"
#include "DrainTestSupport.h"

#include <Log.h>
#include <LogReports.h>
#include <LogTypes.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
namespace pg::log
{
TEST(LogDrain, TimeoutRetriesSameH)
{
    test::DrainOwner owner;
    ASSERT_TRUE(owner.Start());
    owner.Hooks.BlockOutput.store(true);
    const auto writer = owner.System.GetWriter();
    ASSERT_EQ(test::SubmitPaused(owner.System, writer,
                                 [&](const auto& pausedWriter) { return PG_LOG_INFO(pausedWriter, "drain", "first"); }),
              SubmitResult::kAccepted);
    ASSERT_TRUE(test::Wait([&] { return owner.Hooks.OutputEntered.load(); }));
    FlushReport first;
    EXPECT_FALSE(test::Flush(owner.System, std::chrono::steady_clock::now(), first));
    EXPECT_EQ(first.H, 1);
    EXPECT_FALSE(first.Finished);
    EXPECT_EQ(test::SubmitPaused(owner.System, writer,
                                 [&](const auto& pausedWriter) { return PG_LOG_INFO(pausedWriter, "drain", "later"); }),
              SubmitResult::kAccepted);
    owner.Hooks.BlockOutput.store(false);
    FlushReport retried;
    ASSERT_TRUE(test::Flush(owner.System, test::Deadline(), retried));
    EXPECT_EQ(retried.H, first.H);
    EXPECT_EQ(retried.Outputs.at(0).Delivered, 1);
}
TEST(LogDrain, FlushDoesNotFollowLaterRecords)
{
    test::DrainOwner owner;
    ASSERT_TRUE(owner.Start());
    owner.Hooks.BlockOutput.store(true);
    const auto writer = owner.System.GetWriter();
    ASSERT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "drain", "prefix"); }),
              SubmitResult::kAccepted);
    ASSERT_TRUE(test::Wait([&] { return owner.Hooks.OutputEntered.load(); }));
    FlushReport first;
    EXPECT_FALSE(test::Flush(owner.System, std::chrono::steady_clock::now(), first));
    for (int index = 0; index < 32; ++index)
    {
        EXPECT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                     { return PG_LOG_INFO(pausedWriter, "drain", "later {}", index); }),
                  SubmitResult::kAccepted);
    }
    owner.Hooks.BlockOutput.store(false);
    FlushReport retried;
    ASSERT_TRUE(test::Flush(owner.System, test::Deadline(), retried));
    EXPECT_EQ(retried.H, 1);
    EXPECT_EQ(retried.Outputs.at(0).Delivered, 1);
}
TEST(LogDrain, StopRetainsTimedOutFlush)
{
    test::DrainOwner owner;
    ASSERT_TRUE(owner.Start());
    owner.Hooks.BlockOutput.store(true);
    const auto writer = owner.System.GetWriter();
    ASSERT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "drain", "prefix"); }),
              SubmitResult::kAccepted);
    ASSERT_TRUE(test::Wait([&] { return owner.Hooks.OutputEntered.load(); }));
    FlushReport first;
    EXPECT_FALSE(test::Flush(owner.System, std::chrono::steady_clock::now(), first));
    EXPECT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "drain", "later {}", std::string(1100, 'x')); }),
              SubmitResult::kAccepted);
    StopReport pending;
    EXPECT_FALSE(owner.System.Stop(std::chrono::steady_clock::now(), pending));
    owner.Hooks.BlockOutput.store(false);
    StopReport stopped;
    ASSERT_TRUE(owner.System.Stop(test::Deadline(), stopped));
    ASSERT_TRUE(stopped.PreviousFlush.has_value());
    const auto previous = stopped.PreviousFlush.value_or(FlushReport{});
    EXPECT_EQ(previous.H, 1);
    EXPECT_TRUE(previous.OutputComplete);
    EXPECT_EQ(stopped.FinalFlush.H, 2);
    EXPECT_FALSE(stopped.FinalFlush.OutputComplete);
    FlushReport retained;
    ASSERT_TRUE(owner.System.Flush(std::chrono::steady_clock::now(), retained));
    EXPECT_EQ(retained.H, 1);
    EXPECT_TRUE(retained.OutputComplete);
}
TEST(LogDrain, LaterFailureDoesNotPolluteEarlierPrefix)
{
    test::DrainOwner owner;
    ASSERT_TRUE(owner.Start());
    owner.Hooks.BlockOutput.store(true);
    owner.Hooks.BlockSequence.store(2);
    const auto writer = owner.System.GetWriter();
    ASSERT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "drain", "prefix"); }),
              SubmitResult::kAccepted);
    ASSERT_TRUE(test::Wait([&] { return owner.Hooks.OutputEntered.load(); }));
    FlushReport first;
    EXPECT_FALSE(test::Flush(owner.System, std::chrono::steady_clock::now(), first));
    ASSERT_EQ(first.H, 1);
    EXPECT_EQ(test::SubmitPaused(owner.System, writer,
                                 [&](const auto& pausedWriter) { return PG_LOG_INFO(pausedWriter, "drain", "later"); }),
              SubmitResult::kAccepted);
    owner.Hooks.BlockOutput.store(false);
    ASSERT_TRUE(test::Wait([&] { return owner.Hooks.SequenceEntered.load(); }));
    StopReport pending;
    EXPECT_FALSE(owner.System.Stop(std::chrono::steady_clock::now(), pending));
    ASSERT_TRUE(pending.PreviousFlush.has_value());
    const auto previous = pending.PreviousFlush.value_or(FlushReport{});
    EXPECT_TRUE(previous.Finished);
    EXPECT_TRUE(previous.OutputComplete);
    EXPECT_FALSE(pending.ResourcesReleased);
    // 第二条尚未触及 Sink，明确屏障把故障限定在 H 之后。
    owner.Hooks.Console.WriteFailure = MakeLogError(LogError::kIoFailed);
    owner.Hooks.BlockSequence.store(0);
    StopReport stopped;
    ASSERT_TRUE(owner.System.Stop(test::Deadline(), stopped));
    ASSERT_TRUE(stopped.PreviousFlush.has_value());
    EXPECT_TRUE(stopped.PreviousFlush.value_or(FlushReport{}).OutputComplete);
    EXPECT_EQ(stopped.PreviousFlush.value_or(FlushReport{}).Outputs.at(0).Undelivered, 0);
    EXPECT_EQ(stopped.FinalFlush.H, 2);
    EXPECT_EQ(stopped.FinalFlush.Outputs.at(0).Delivered, 1);
    EXPECT_EQ(stopped.FinalFlush.Outputs.at(0).Undelivered, 1);
    EXPECT_FALSE(stopped.FinalFlush.OutputComplete);
}
TEST(LogDrain, RejectionsAfterCaptureCannotRewriteOldPrefix)
{
    test::DrainOwner owner;
    ASSERT_TRUE(owner.Start(2));
    owner.Hooks.BlockOutput.store(true);
    owner.Hooks.BlockSequence.store(2);
    const auto writer = owner.System.GetWriter();
    ASSERT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "drain", "prefix"); }),
              SubmitResult::kAccepted);
    ASSERT_TRUE(test::Wait([&] { return owner.Hooks.OutputEntered.load(); }));
    FlushReport first;
    EXPECT_FALSE(owner.System.Flush(std::chrono::steady_clock::now(), first));
    ASSERT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "drain", "second"); }),
              SubmitResult::kAccepted);
    owner.Hooks.BlockOutput.store(false);
    ASSERT_TRUE(test::Wait([&] { return owner.Hooks.SequenceEntered.load(); }));
    ASSERT_EQ(test::SubmitPaused(owner.System, writer,
                                 [&](const auto& pausedWriter) { return PG_LOG_INFO(pausedWriter, "drain", "third"); }),
              SubmitResult::kAccepted);
    ASSERT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "drain", "fourth"); }),
              SubmitResult::kAccepted);
    EXPECT_EQ(PG_LOG_INFO(writer, "drain", "rejected"), SubmitResult::kDropped);
    FlushReport retried;
    ASSERT_TRUE(owner.System.Flush(test::Deadline(), retried));
    EXPECT_EQ(retried.H, 1);
    EXPECT_EQ(retried.Rejected.at(2).Capacity, 0);
    EXPECT_TRUE(retried.OutputComplete);
    EXPECT_EQ(owner.System.Inspect().Rejected.at(2).Capacity, 1);
    owner.Hooks.BlockSequence.store(0);
    StopReport stopped;
    ASSERT_TRUE(owner.System.Stop(test::Deadline(), stopped));
    EXPECT_EQ(stopped.FinalFlush.H, 4);
    EXPECT_EQ(stopped.FinalFlush.Rejected.at(2).Capacity, 1);
    EXPECT_FALSE(stopped.FinalFlush.OutputComplete);
    EXPECT_EQ(PG_LOG_INFO(writer, "drain", "closed"), SubmitResult::kClosed);
    StopReport repeated;
    ASSERT_TRUE(owner.System.Stop(std::chrono::steady_clock::now(), repeated));
    EXPECT_EQ(repeated.FinalFlush.Rejected.at(2).Capacity, 1);
    EXPECT_EQ(repeated.FinalFlush.H, 4);
}
TEST(LogDrain, ContinuousProducerCannotExtendPendingFlush)
{
    test::DrainOwner owner;
    ASSERT_TRUE(owner.Start(8));
    owner.Hooks.BlockOutput.store(true);
    owner.Hooks.BlockSequence.store(2);
    const auto writer = owner.System.GetWriter();
    ASSERT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "drain", "prefix"); }),
              SubmitResult::kAccepted);
    ASSERT_TRUE(test::Wait([&] { return owner.Hooks.OutputEntered.load(); }));
    FlushReport pending;
    EXPECT_FALSE(owner.System.Flush(std::chrono::steady_clock::now(), pending));
    ASSERT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "drain", "second"); }),
              SubmitResult::kAccepted);
    owner.Hooks.BlockOutput.store(false);
    ASSERT_TRUE(test::Wait([&] { return owner.Hooks.SequenceEntered.load(); }));
    std::atomic<bool> running{true};
    std::atomic<std::uint64_t> attempts{};
    std::uint64_t accepted{};
    std::uint64_t dropped{};
    std::uint64_t unexpected{};
    std::jthread producer(
        [&]
        {
            while (running.load())
            {
                const auto result = PG_LOG_INFO(writer, "producer", "later");
                if (result == SubmitResult::kAccepted)
                {
                    ++accepted;
                }
                else if (result == SubmitResult::kDropped)
                {
                    ++dropped;
                }
                else
                {
                    ++unexpected;
                }
                attempts.fetch_add(1);
            }
        });
    const bool produced = test::Wait([&] { return attempts.load() >= 64; });
    FlushReport report;
    const auto flushed = owner.System.Flush(test::Deadline(), report);
    running.store(false);
    producer.join();
    ASSERT_TRUE(produced);
    ASSERT_TRUE(flushed);
    EXPECT_EQ(report.H, 1);
    EXPECT_TRUE(report.OutputComplete);
    EXPECT_EQ(accepted, 8);
    EXPECT_GT(dropped, 0);
    EXPECT_EQ(unexpected, 0);
    owner.Hooks.BlockSequence.store(0);
    StopReport stopped;
    ASSERT_TRUE(owner.System.Stop(test::Deadline(), stopped));
    EXPECT_EQ(stopped.FinalFlush.H, 10);
    EXPECT_EQ(stopped.FinalFlush.Rejected.at(2).Capacity, dropped);
}
TEST(LogDrain, StopWaitsForProtectedSubmissionBeforeFixingFinalH)
{
    test::DrainOwner owner;
    ASSERT_TRUE(owner.Start());
    owner.Hooks.BlockOutput.store(true);
    const auto writer = owner.System.GetWriter();
    ASSERT_EQ(test::SubmitPaused(owner.System, writer, [&](const auto& pausedWriter)
                                 { return PG_LOG_INFO(pausedWriter, "drain", "prefix"); }),
              SubmitResult::kAccepted);
    ASSERT_TRUE(test::Wait([&] { return owner.Hooks.OutputEntered.load(); }));
    FlushReport prefix;
    EXPECT_FALSE(owner.System.Flush(std::chrono::steady_clock::now(), prefix));
    std::atomic<bool> entered{};
    std::atomic<bool> release{};
    SubmitResult submitted{SubmitResult::kDisabled};
    std::jthread producer(
        [&]
        {
            submitted = PG_LOG_INFO(writer, "drain", "{}",
                                    (
                                        [&]
                                        {
                                            entered.store(true);
                                            while (!release.load())
                                            {
                                                std::this_thread::yield();
                                            }
                                            return 7;
                                        }()));
        });
    const bool protectedSubmission = test::Wait([&] { return entered.load(); });
    StopReport pending;
    const auto firstStop = owner.System.Stop(std::chrono::steady_clock::now(), pending);
    EXPECT_EQ(PG_LOG_INFO(writer, "drain", "closed"), SubmitResult::kClosed);
    release.store(true);
    producer.join();
    owner.Hooks.BlockOutput.store(false);
    ASSERT_TRUE(protectedSubmission);
    EXPECT_FALSE(firstStop);
    EXPECT_FALSE(pending.ResourcesReleased);
    EXPECT_EQ(submitted, SubmitResult::kAccepted);
    StopReport stopped;
    ASSERT_TRUE(owner.System.Stop(test::Deadline(), stopped));
    ASSERT_TRUE(stopped.PreviousFlush.has_value());
    EXPECT_EQ(stopped.PreviousFlush.value_or(FlushReport{}).H, 1);
    EXPECT_EQ(stopped.FinalFlush.H, 2);
    EXPECT_EQ(stopped.FinalFlush.Outputs.at(0).Delivered, 2);
    EXPECT_TRUE(stopped.FinalFlush.OutputComplete);
}
} // namespace pg::log
