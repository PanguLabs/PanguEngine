#include <LogTypes.h>
#include <NativeLogIo.h>
#include <SpdlogBackend.h>
#include <gtest/gtest.h>
#include <spdlog/common.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/spdlog.h>

#include <array>
#include <string_view>

namespace pg::log::detail
{
namespace
{
struct Capture
{
    // NOLINTNEXTLINE(readability-redundant-member-init) 保持部分指定初始化无诊断。
    std::string_view Payload{};
    unsigned int Writes{};
    unsigned int Flushes{};
    bool Fail{};

    static OutputOutcome Write(void* context, std::string_view text) noexcept
    {
        auto& capture = *static_cast<Capture*>(context);
        ++capture.Writes;
        capture.Payload = text;
        if (capture.Fail)
        {
            return {.Failure = MakeLogError(LogError::kIoFailed)};
        }
        return {.BytesWritten = text.size(), .Complete = true};
    }
    static OutputOutcome Flush(void* context) noexcept
    {
        auto& capture = *static_cast<Capture*>(context);
        ++capture.Flushes;
        return {.Complete = true};
    }
    [[nodiscard]] OutputPort Port() noexcept { return {.Context = this, .Write = Write, .Flush = Flush}; }
};

TEST(Backend, EmptyConstructionNeedsNoHeap)
{
    const OutputPort empty;
    SpdlogBackend backend{empty, empty};
    const auto result = backend.Emit(Level::kInfo, "unused");
    EXPECT_FALSE(std::get<0>(result).Failure);
    EXPECT_FALSE(std::get<1>(result).Failure);
    EXPECT_FALSE(std::get<0>(result).Complete);
}

TEST(Backend, RawPayloadPreservesBraces)
{
    Capture capture;
    SpdlogBackend backend{capture.Port(), {}};
    constexpr auto kBytes = std::to_array("literal {} {broken} {{}}\0tail");
    const std::string_view kText{kBytes.data(), kBytes.size() - 1};
    const auto result = backend.Emit(Level::kTrace, kText);
    EXPECT_TRUE(std::get<0>(result).Complete);
    EXPECT_EQ(capture.Payload, kText);
    EXPECT_EQ(capture.Writes, 1U);
    EXPECT_TRUE(std::get<0>(backend.Flush()).Complete);
    EXPECT_EQ(capture.Flushes, 1U);
}

TEST(Backend, FileFailureDoesNotAbortConsole)
{
    Capture console;
    Capture file{.Fail = true};
    SpdlogBackend backend{console.Port(), file.Port()};
    const auto result = backend.Emit(Level::kError, "{} raw");
    EXPECT_EQ(console.Writes, 1U);
    EXPECT_TRUE(std::get<0>(result).Complete);
    ASSERT_TRUE(std::get<1>(result).Failure);
    const auto failure = std::get<1>(result).Failure;
    if (failure)
    {
        EXPECT_EQ(*failure, MakeLogError(LogError::kIoFailed));
    }
    EXPECT_FALSE(std::get<1>(result).Complete);
}

TEST(Backend, OffAndInvalidLevelsDoNotReuseEarlierOutcomes)
{
    Capture console;
    Capture file;
    SpdlogBackend backend{console.Port(), file.Port()};
    for (const auto level : {Level::kTrace, Level::kDebug, Level::kInfo, Level::kWarning, Level::kError})
    {
        const auto result = backend.Emit(level, "level");
        EXPECT_TRUE(std::get<0>(result).Complete);
        EXPECT_TRUE(std::get<1>(result).Complete);
    }
    // 固定 uint8_t 底层允许构造未知值，验证私有边界拒绝该输入。
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    for (const auto level : {Level::kOff, static_cast<Level>(255)})
    {
        const auto result = backend.Emit(level, "off");
        EXPECT_FALSE(std::get<0>(result).Complete);
        EXPECT_FALSE(std::get<1>(result).Complete);
        EXPECT_EQ(std::get<0>(result).BytesWritten, 0U);
        EXPECT_EQ(std::get<1>(result).BytesWritten, 0U);
    }
    EXPECT_EQ(console.Writes, 5U);
    EXPECT_EQ(file.Writes, 5U);
}

TEST(Backend, NoGlobalRegistry)
{
    Capture first;
    Capture second;
    SpdlogBackend backend{first.Port(), {}};
    SpdlogBackend other{second.Port(), {}};
    static_cast<void>(backend.Emit(Level::kInfo, "first"));
    static_cast<void>(other.Emit(Level::kInfo, "second"));
    EXPECT_EQ(first.Payload, "first");
    EXPECT_EQ(second.Payload, "second");
    EXPECT_EQ(first.Writes, 1U);
    EXPECT_EQ(second.Writes, 1U);
    EXPECT_EQ(spdlog::get(""), nullptr);
    EXPECT_EQ(spdlog::default_logger(), nullptr);
}

TEST(Backend, RawSinkRejectsFormatterConfiguration)
{
    Capture capture;
    const spdlog::details::log_msg message{spdlog::source_loc{}, "", spdlog::level::info, "raw"};
    RawSink patternSink{capture.Port()};
    patternSink.set_pattern("%v");
    patternSink.log(message);
    EXPECT_EQ(patternSink.Outcome().Failure, MakeLogError(LogError::kInvalidConfig));
    patternSink.flush();
    EXPECT_EQ(patternSink.Outcome().Failure, MakeLogError(LogError::kInvalidConfig));
    RawSink formatterSink{capture.Port()};
    formatterSink.set_formatter(nullptr);
    formatterSink.log(message);
    EXPECT_EQ(formatterSink.Outcome().Failure, MakeLogError(LogError::kInvalidConfig));
    EXPECT_EQ(capture.Writes, 0U);
    EXPECT_EQ(capture.Flushes, 0U);
}
} // namespace
} // namespace pg::log::detail
