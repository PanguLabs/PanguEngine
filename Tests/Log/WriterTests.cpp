#include "WriterTestAccess.h"
#include <Log.h>
#include <LogQueue.h>
#include <LogRecord.h>
#include <LogTypes.h>
#include <LogWriter.h>
#include <WriterControl.h>

#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <string_view>
#include <utility>

namespace pg::log
{
TEST(LogWriter, ReferenceOverflowDoesNotWrap)
{
    detail::LogQueue queue;
    ASSERT_TRUE(queue.Initialize(4, 1));
    detail::WriterControlOwner owner;
    ASSERT_TRUE(owner.Initialize(queue, Level::kInfo));
    const auto writer = owner.GetWriter();
    const auto previous = detail::WriterTestAccess::SetReferences(owner, std::numeric_limits<std::uint64_t>::max());
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) 验证复制引用的溢出路径。
    const auto refused = writer;
    EXPECT_TRUE(owner.Inspect().CounterOverflow);
    EXPECT_EQ(PG_LOG_ERROR(refused, "test", "unavailable"), SubmitResult::kDisabled);
    EXPECT_EQ(detail::WriterTestAccess::SetReferences(owner, previous), std::numeric_limits<std::uint64_t>::max());
}

// 此用例明确验证移动后的空句柄契约，分析器不认识自定义移动语义。
// NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
TEST(LogWriter, MoveLeavesDisabledSource)
{
    detail::LogQueue queue;
    ASSERT_TRUE(queue.Initialize(4, 1));
    detail::WriterControlOwner owner;
    ASSERT_TRUE(owner.Initialize(queue, Level::kInfo));
    auto source = owner.GetWriter();
    auto moved = std::move(source);
    EXPECT_EQ(PG_LOG_ERROR(source, "test", "disabled"), SubmitResult::kDisabled);
    LogWriter assigned;
    assigned = std::move(moved);
    EXPECT_EQ(PG_LOG_ERROR(moved, "test", "disabled"), SubmitResult::kDisabled);
    EXPECT_EQ(PG_LOG_ERROR(assigned, "test", "accepted"), SubmitResult::kAccepted);
}
// NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move)

TEST(LogWriter, FilterBeforeArgumentEvaluation)
{
    detail::LogQueue queue;
    ASSERT_TRUE(queue.Initialize(4, 1));
    detail::WriterControlOwner owner;
    ASSERT_TRUE(owner.Initialize(queue, Level::kWarning));
    auto writer = owner.GetWriter();
    int expensiveCalls{};
    int writerCalls{};
    int levelCalls{};
    const auto result = PG_LOG((++levelCalls, Level::kInfo), (++writerCalls, writer), "test", "{}", ++expensiveCalls);
    EXPECT_EQ(result, SubmitResult::kFiltered);
    EXPECT_EQ(expensiveCalls, 0);
    EXPECT_EQ(writerCalls, 1);
    EXPECT_EQ(levelCalls, 1);
}

TEST(LogWriter, SurvivesInstanceOwner)
{
    LogWriter stale;
    {
        detail::LogQueue queue;
        ASSERT_TRUE(queue.Initialize(4, 1));
        detail::WriterControlOwner owner;
        ASSERT_TRUE(owner.Initialize(queue, Level::kInfo));
        stale = owner.GetWriter();
        EXPECT_EQ(PG_LOG_INFO(stale, "test", "owned {}", 42), SubmitResult::kAccepted);
    }
    LogTags tags;
    tags.Operation = 17;
    auto copy = stale.WithTags(tags);
    int calls{};
    const auto staleResult = PG_LOG_ERROR(copy, "test", "{}", ++calls);
    EXPECT_EQ(staleResult, SubmitResult::kClosed);
    EXPECT_EQ(calls, 0);
}

TEST(LogWriter, CopyDoesNotPreventClose)
{
    detail::LogQueue queue;
    ASSERT_TRUE(queue.Initialize(4, 1));
    detail::WriterControlOwner owner;
    ASSERT_TRUE(owner.Initialize(queue, Level::kInfo));
    const auto writer = owner.GetWriter();
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) 独立副本不得阻止关闭。
    const auto copy = writer;
    owner.Close();
    EXPECT_TRUE(owner.IsQuiescent());
    EXPECT_TRUE(owner.Reset());
    EXPECT_EQ(PG_LOG_ERROR(copy, "test", "closed"), SubmitResult::kClosed);
}

TEST(LogWriter, OwnsContextAndTags)
{
    detail::LogQueue queue;
    ASSERT_TRUE(queue.Initialize(4, 1));
    detail::WriterControlOwner owner;
    ASSERT_TRUE(owner.Initialize(queue, Level::kInfo));
    LogTags tags;
    tags.Operation = 7;
    auto writer = owner.GetWriter().WithTags(tags);
    EXPECT_EQ(PG_LOG_INFO(writer, "category", "value {}", 42), SubmitResult::kAccepted);
    LogRecord record;
    ASSERT_TRUE(queue.TryPop(record));
    EXPECT_EQ(record.Message(), "value 42");
    EXPECT_EQ(record.Category(), "category");
    EXPECT_EQ(record.Tags.Operation, 7);
    EXPECT_NE(record.SourceLine, 0);
    EXPECT_FALSE(record.File().empty());
    EXPECT_NE(record.Function().find("OwnsContextAndTags"), std::string_view::npos);
    EXPECT_EQ(record.Function().find("operator()"), std::string_view::npos);
    EXPECT_EQ(PG_LOG_INFO(LogWriter{}, "disabled", "none"), SubmitResult::kDisabled);
}
} // namespace pg::log
