#include "WriterTestAccess.h"
#include <LogQueue.h>
#include <LogRecord.h>
#include <LogTypes.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <utility>

namespace pg::log
{
TEST(LogQueue, BorrowsInstanceSlotStorage)
{
    std::array<LogRecord, 4> slots;
    {
        detail::LogQueue queue;
        ASSERT_TRUE(queue.Initialize(slots, 1));
        LogRecord record;
        // NOLINTNEXTLINE(performance-move-const-arg) 验证消费 rvalue 的接纳接口。
        EXPECT_EQ(queue.TryPush(std::move(record)), SubmitResult::kAccepted);
        EXPECT_EQ(slots.at(0).Sequence, 1);
    }
    EXPECT_EQ(slots.at(0).Sequence, 1);
}

namespace
{
LogRecord Record(Level level)
{
    LogRecord record;
    record.Severity = level;
    return record;
}
} // namespace

TEST(LogQueue, ReserveCannotBeConsumedByInfo)
{
    detail::LogQueue queue;
    ASSERT_TRUE(queue.Initialize(4, 2));
    EXPECT_EQ(queue.TryPush(Record(Level::kInfo)), SubmitResult::kAccepted);
    EXPECT_EQ(queue.TryPush(Record(Level::kInfo)), SubmitResult::kAccepted);
    EXPECT_EQ(queue.TryPush(Record(Level::kInfo)), SubmitResult::kDropped);
    EXPECT_EQ(queue.TryPush(Record(Level::kWarning)), SubmitResult::kAccepted);
    EXPECT_EQ(queue.TryPush(Record(Level::kError)), SubmitResult::kAccepted);
    EXPECT_EQ(queue.TryPush(Record(Level::kError)), SubmitResult::kDropped);
    const auto snapshot = queue.Inspect();
    EXPECT_EQ(snapshot.HighestAccepted, 4);
    EXPECT_EQ(snapshot.Rejected.at(2).Capacity, 1);
    EXPECT_EQ(snapshot.Rejected.at(4).Capacity, 1);
}

TEST(LogQueue, ContendedErrorMayDrop)
{
    detail::LogQueue queue;
    ASSERT_TRUE(queue.Initialize(4, 1));
    detail::WriterTestAccess::Lock(queue);
    EXPECT_EQ(queue.TryPush(Record(Level::kError)), SubmitResult::kDropped);
    detail::WriterTestAccess::Unlock(queue);
    const auto snapshot = queue.Inspect();
    EXPECT_EQ(snapshot.HighestAccepted, 0);
    EXPECT_EQ(snapshot.Rejected.at(4).Contention, 1);
}

TEST(LogQueue, NoEvictionOrSequenceReuse)
{
    detail::LogQueue queue;
    ASSERT_TRUE(queue.Initialize(2, 0));
    EXPECT_EQ(queue.TryPush(Record(Level::kInfo)), SubmitResult::kAccepted);
    EXPECT_EQ(queue.TryPush(Record(Level::kInfo)), SubmitResult::kAccepted);
    const auto before = queue.Inspect();
    EXPECT_EQ(queue.TryPush(Record(Level::kError)), SubmitResult::kDropped);
    const auto after = queue.Inspect();
    EXPECT_EQ(after.HighestAccepted, before.HighestAccepted);
    LogRecord record;
    ASSERT_TRUE(queue.TryPop(record));
    EXPECT_EQ(record.Sequence, 1);
    EXPECT_EQ(record.Severity, Level::kInfo);
    EXPECT_EQ(queue.TryPush(Record(Level::kError)), SubmitResult::kAccepted);
    ASSERT_TRUE(queue.TryPop(record));
    EXPECT_EQ(record.Sequence, 2);
    ASSERT_TRUE(queue.TryPop(record));
    EXPECT_EQ(record.Sequence, 3);
    EXPECT_FALSE(queue.TryPop(record));
}

TEST(LogQueue, CountersExposeOverflow)
{
    detail::LogQueue queue;
    ASSERT_TRUE(queue.Initialize(2, 0));
    detail::WriterTestAccess::SeedCapacityCounter(queue, Level::kError, std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(queue.TryPush(Record(Level::kError)), SubmitResult::kAccepted);
    EXPECT_EQ(queue.TryPush(Record(Level::kError)), SubmitResult::kAccepted);
    EXPECT_EQ(queue.TryPush(Record(Level::kError)), SubmitResult::kDropped);
    EXPECT_TRUE(queue.Inspect().CounterOverflow);
    EXPECT_EQ(queue.Inspect().Rejected.at(4).Capacity, std::numeric_limits<std::uint64_t>::max());
}

TEST(LogQueue, SequenceExhaustionStopsAdmission)
{
    detail::LogQueue queue;
    ASSERT_TRUE(queue.Initialize(2, 0));
    detail::WriterTestAccess::SeedSequence(queue, std::numeric_limits<std::uint64_t>::max() - 1);
    EXPECT_EQ(queue.TryPush(Record(Level::kError)), SubmitResult::kAccepted);
    EXPECT_TRUE(queue.Inspect().SequenceExhausted);
    EXPECT_EQ(queue.TryPush(Record(Level::kError)), SubmitResult::kDropped);
    const auto snapshot = queue.Inspect();
    EXPECT_TRUE(snapshot.SequenceExhausted);
    EXPECT_EQ(snapshot.HighestAccepted, std::numeric_limits<std::uint64_t>::max());
    EXPECT_EQ(snapshot.Rejected.at(4).SequenceExhausted, 1);
}
} // namespace pg::log
