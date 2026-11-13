#include <Log.h>
#include <LogQueue.h>
#include <LogRecord.h>
#include <LogTypes.h>
#include <WriterControl.h>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>

namespace pg::log
{
TEST(LogQueue, ConcurrentProducersPreserveEveryAcceptedSequence)
{
    constexpr std::size_t kProducers = 4;
    constexpr std::size_t kAttempts = 512;
    detail::LogQueue queue;
    ASSERT_TRUE(queue.Initialize(32, 4));
    std::array<std::array<SubmitResult, kAttempts>, kProducers> results{};
    std::array<std::jthread, kProducers> producers;
    std::atomic<std::size_t> running{kProducers};
    std::barrier start{static_cast<std::ptrdiff_t>(kProducers + 1)};
    for (std::size_t producer = 0; producer < kProducers; ++producer)
    {
        producers.at(producer) = std::jthread(
            [&, producer]
            {
                start.arrive_and_wait();
                for (std::size_t attempt = 0; attempt < kAttempts; ++attempt)
                {
                    LogRecord record;
                    record.Severity = Level::kError;
                    // NOLINTNEXTLINE(performance-move-const-arg) 接纳接口明确消费 rvalue。
                    results.at(producer).at(attempt) = queue.TryPush(std::move(record));
                }
                running.fetch_sub(1);
            });
    }
    start.arrive_and_wait();
    std::uint64_t processed{};
    LogRecord record;
    while (running.load() != 0)
    {
        if (queue.TryPop(record))
        {
            EXPECT_EQ(record.Sequence, ++processed);
        }
        else
        {
            std::this_thread::yield();
        }
    }
    for (auto& producer : producers)
    {
        producer.join();
    }
    while (queue.TryPop(record))
    {
        EXPECT_EQ(record.Sequence, ++processed);
    }
    std::uint64_t accepted{};
    std::uint64_t dropped{};
    for (const auto& batch : results)
    {
        for (const auto result : batch)
        {
            EXPECT_TRUE(result == SubmitResult::kAccepted || result == SubmitResult::kDropped);
            accepted += result == SubmitResult::kAccepted ? 1 : 0;
            dropped += result == SubmitResult::kDropped ? 1 : 0;
        }
    }
    const auto snapshot = queue.Inspect();
    EXPECT_EQ(accepted + dropped, kProducers * kAttempts);
    EXPECT_EQ(processed, accepted);
    EXPECT_EQ(snapshot.HighestAccepted, accepted);
    EXPECT_EQ(snapshot.Pending, 0);
    const auto counted = snapshot.Rejected.at(4).Capacity + snapshot.Rejected.at(4).Contention;
    EXPECT_LE(counted, dropped);
    EXPECT_TRUE(counted == dropped || snapshot.CounterOverflow);
}

TEST(LogWriter, ContendedBuildSkipsArguments)
{
    detail::LogQueue queue;
    ASSERT_TRUE(queue.Initialize(4, 1));
    detail::WriterControlOwner owner;
    ASSERT_TRUE(owner.Initialize(queue, Level::kInfo));
    const auto writer = owner.GetWriter();
    std::barrier entered{2};
    std::barrier resume{2};
    SubmitResult result{SubmitResult::kDisabled};
    std::jthread producer(
        [&]
        {
            result = PG_LOG_INFO(writer, "test", "{}",
                                 (
                                     [&]
                                     {
                                         entered.arrive_and_wait();
                                         resume.arrive_and_wait();
                                         return 42;
                                     }()));
        });
    entered.arrive_and_wait();
    int calls{};
    EXPECT_EQ(PG_LOG_ERROR(writer, "contended", "{}", ++calls), SubmitResult::kDropped);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(owner.Inspect().Contention.at(4), 1);
    resume.arrive_and_wait();
    producer.join();
    EXPECT_EQ(result, SubmitResult::kAccepted);
}

TEST(LogWriter, InFlightBuildPreventsRelease)
{
    detail::LogQueue queue;
    ASSERT_TRUE(queue.Initialize(4, 1));
    detail::WriterControlOwner owner;
    ASSERT_TRUE(owner.Initialize(queue, Level::kInfo));
    const auto writer = owner.GetWriter();
    std::barrier entered{2};
    std::barrier resume{2};
    SubmitResult result{SubmitResult::kDisabled};
    std::jthread producer(
        [&]
        {
            result = PG_LOG_INFO(writer, "test", "{}",
                                 (
                                     [&]
                                     {
                                         entered.arrive_and_wait();
                                         resume.arrive_and_wait();
                                         return 42;
                                     }()));
        });
    entered.arrive_and_wait();
    owner.Close();
    EXPECT_FALSE(owner.IsQuiescent());
    EXPECT_FALSE(owner.Reset());
    int calls{};
    EXPECT_EQ(PG_LOG_ERROR(writer, "closed", "{}", ++calls), SubmitResult::kClosed);
    EXPECT_EQ(calls, 0);
    resume.arrive_and_wait();
    producer.join();
    EXPECT_EQ(result, SubmitResult::kAccepted);
    EXPECT_TRUE(owner.IsQuiescent());
    EXPECT_TRUE(owner.Reset());
    LogRecord record;
    ASSERT_TRUE(queue.TryPop(record));
    EXPECT_EQ(record.Message(), "42");
}

TEST(LogWriter, EveryCloseRaceHasSafeOutcome)
{
    for (int iteration = 0; iteration < 128; ++iteration)
    {
        detail::LogQueue queue;
        ASSERT_TRUE(queue.Initialize(4, 1));
        detail::WriterControlOwner owner;
        ASSERT_TRUE(owner.Initialize(queue, Level::kInfo));
        const auto writer = owner.GetWriter();
        std::barrier start{2};
        SubmitResult result{SubmitResult::kDisabled};
        int calls{};
        std::jthread producer(
            [&]
            {
                start.arrive_and_wait();
                result = PG_LOG_ERROR(writer, "race", "{}", ++calls);
            });
        start.arrive_and_wait();
        owner.Close();
        producer.join();
        EXPECT_TRUE(owner.IsQuiescent());
        EXPECT_TRUE(result == SubmitResult::kAccepted || result == SubmitResult::kClosed);
        EXPECT_EQ(calls, result == SubmitResult::kAccepted ? 1 : 0);
        EXPECT_EQ(queue.Inspect().HighestAccepted, result == SubmitResult::kAccepted ? 1 : 0);
        EXPECT_TRUE(owner.Reset());
        EXPECT_EQ(PG_LOG_ERROR(writer, "stale", "closed"), SubmitResult::kClosed);
    }
}
} // namespace pg::log
