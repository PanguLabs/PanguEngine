#include "Fixtures/FormatCompile/UserValue.h"
#include "FormatAllocationCases.h"
#include <Error.h>
#include <LogFormat.h>
#include <LogModule.h>
#include <LogRecord.h>
#include <LogTypes.h>
#include <ModuleIdentity.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <source_location>
#include <string>
#include <string_view>

namespace
{
enum class SampleCode : std::int64_t
{
    kMin = std::numeric_limits<std::int64_t>::min(),
    kMax = std::numeric_limits<std::int64_t>::max(),
};

template <typename T>
bool CheckScalar(T value, std::string_view expected) noexcept
{
    pg::log::LogRecord record;
    pg::log::detail::FormatRecord(record, "{}", value);
    return record.Message() == expected && !record.ConstructionFailed && !record.Truncated;
}

bool CheckAssociatedConversions() noexcept
{
    log_format_fixture::gCalls = {};
    const bool stringCode = CheckScalar(log_format_fixture::StringCode::kValue, "37");
    const bool formatterCode = CheckScalar(log_format_fixture::FormatterCode::kValue, "43");
    const auto calls = log_format_fixture::gCalls;
    return stringCode && formatterCode && calls.Normalize == 0 && calls.Value == 0 && calls.Formatter == 0;
}
template <typename T>
bool CheckFloat() noexcept
{
    for (const auto value : {
             std::numeric_limits<T>::lowest(),
             std::numeric_limits<T>::max(),
             std::numeric_limits<T>::min(),
             std::numeric_limits<T>::denorm_min(),
             T{0.0},
             T{-0.0},
             std::numeric_limits<T>::infinity(),
             -std::numeric_limits<T>::infinity(),
             std::numeric_limits<T>::quiet_NaN(),
         })
    {
        pg::log::LogRecord record;
        pg::log::detail::FormatRecord(record, "{}", value);
        if (record.Message().empty() || record.ConstructionFailed || record.Truncated)
        {
            return false;
        }
    }
    return true;
}
} // namespace

// 与独立无分配进程共用极值场景，不复制分配替换或引入产品测试导出。
bool RunFormatAllocationCases(const std::string& ownedText) noexcept
{
    const bool associatedConversionsSafe = CheckAssociatedConversions();
    bool success =
        CheckScalar(true, "true") && CheckScalar(false, "false") && CheckScalar('x', "x") &&
        CheckScalar(std::numeric_limits<signed char>::min(), "-128") &&
        CheckScalar(std::numeric_limits<unsigned char>::max(), "255") &&
        CheckScalar(std::numeric_limits<short>::min(), "-32768") &&
        CheckScalar(std::numeric_limits<unsigned short>::max(), "65535") &&
        CheckScalar(std::numeric_limits<int>::min(), "-2147483648") &&
        CheckScalar(std::numeric_limits<unsigned int>::max(), "4294967295") &&
        CheckScalar(std::numeric_limits<long>::min(), sizeof(long) == 8 ? "-9223372036854775808" : "-2147483648") &&
        CheckScalar(std::numeric_limits<unsigned long>::max(),
                    sizeof(long) == 8 ? "18446744073709551615" : "4294967295") &&
        CheckScalar(std::numeric_limits<long long>::min(), "-9223372036854775808") &&
        CheckScalar(std::numeric_limits<unsigned long long>::max(), "18446744073709551615") &&
        CheckScalar(SampleCode::kMin, "-9223372036854775808") && CheckFloat<float>() && CheckFloat<double>();
    success = success && CheckScalar(std::numeric_limits<signed char>::max(), "127") &&
              CheckScalar(std::numeric_limits<short>::max(), "32767") &&
              CheckScalar(std::numeric_limits<int>::max(), "2147483647") &&
              CheckScalar(std::numeric_limits<long>::max(), sizeof(long) == 8 ? "9223372036854775807" : "2147483647") &&
              CheckScalar(std::numeric_limits<long long>::max(), "9223372036854775807") && CheckScalar(0U, "0") &&
              CheckScalar(0ULL, "0") && CheckScalar(static_cast<unsigned char>(0), "0") &&
              CheckScalar(static_cast<unsigned short>(0), "0") && CheckScalar(0UL, "0") &&
              CheckScalar(SampleCode::kMax, "9223372036854775807") &&
              CheckScalar(std::numeric_limits<char>::min(), std::numeric_limits<char>::is_signed ? "\\x80" : "\\x00") &&
              CheckScalar(std::numeric_limits<char>::max(), std::numeric_limits<char>::is_signed ? "\\x7f" : "\\xff") &&
              CheckScalar(1.25F, "1.25") && CheckScalar(1.25, "1.25");
    const pg::Error error{pg::log::kLogErrorDomain, 5, {.Source = pg::NativeErrorSourceId{7}, .Value = -42}};
    pg::log::LogRecord record;
    record.Failure = error;
    pg::log::detail::FormatRecord(record, "{} {}", pg::log::kLogModuleId, error);
    success =
        success && record.Message().contains("b6ddc3fdc5b34a2e908c566d90db1e21") && record.Message().contains("-42");
    std::array<char, 4096> text{};
    text.fill('a');
    pg::log::detail::FormatRecord(record, "{}", std::string_view{text.data(), text.size()});
    success = success && record.Truncated && record.Failure == error && record.Message().size() == 1024;
    record = {};
    pg::log::detail::FormatRecord(record, "{}", ownedText);
    success = success && record.Truncated && record.Message().size() == 1024;
    record = {};
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays) 验证已知边界的非零结尾 C 数组。
    constexpr char kUnterminated[]{'o', 'k'};
    pg::log::detail::FormatRecord(record, "{} {}", "literal", kUnterminated);
    success = success && record.Message() == "literal ok" && !record.Truncated;
    record.Failure = error;
    pg::log::EncodedLogLine line;
    pg::log::detail::EncodeRecord(record, line);
    success = success && line.Text().find("error=") < line.Text().find("message=") && !line.Text().empty();
    constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
    const pg::Error maximumError{pg::ErrorDomainId::Constant<kMax>(),
                                 std::numeric_limits<std::uint32_t>::max(),
                                 {
                                     .Source = pg::NativeErrorSourceId{kMax},
                                     .Value = std::numeric_limits<std::int64_t>::min(),
                                 }};
    pg::module::ModuleId module;
    module.Bytes.fill(std::byte{0xff});
    pg::log::detail::SetRecordContext(
        record, pg::log::Level::kError, ownedText, std::source_location::current(), maximumError,
        {
            .Module = module,
            .Scope = pg::log::LogScopeTag{.Domain = kMax, .Slot = 0xffffffffU, .Generation = 0xffffffffU},
            .Declaration = module.Bytes,
            .Operation = kMax,
        });
    record.Sequence = kMax;
    record.FileText.Assign(ownedText, record.Truncated);
    record.FunctionText.Assign(ownedText, record.Truncated);
    pg::log::detail::FormatRecord(record, "{} {}", module, maximumError);
    success =
        success && record.Message().contains("ffffffffffffffffffffffffffffffff") &&
        record.Message().contains("18446744073709551615:4294967295 native=18446744073709551615:-9223372036854775808");
    pg::log::detail::FormatRecord(record, "{}", ownedText);
    pg::log::detail::EncodeRecord(record, line);
    success = success && record.Failure == maximumError && record.Truncated && !line.Truncated &&
              line.Text().contains("scope=18446744073709551615:4294967295:4294967295") &&
              line.Text().contains("operation=18446744073709551615");
    pg::log::detail::FormatRecord(record, "{:.10000f}", std::numeric_limits<double>::max());
    success = success && record.ConstructionFailed && record.Failure == maximumError;
    pg::log::detail::FormatRecord(record, "{:L}", 12345);
    success = success && record.Message() == "[log-format-rejected]";
    record = {};
    pg::log::detail::FormatRecord(record, "{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}", ownedText, ownedText, ownedText,
                                  ownedText, ownedText, ownedText, ownedText, ownedText, ownedText, ownedText,
                                  ownedText, ownedText, ownedText, ownedText, ownedText, ownedText);
    success = success && record.Truncated && !record.ConstructionFailed && record.Message().size() == 1024;
    record = {};
    pg::log::detail::FormatRecord(record, "{}", std::string_view{"\xc2\x85\xe2\x80\xa8\xe2\x80\xa9"});
    return success && record.Message() == R"(\u0085\u2028\u2029)" && associatedConversionsSafe;
}

#ifndef PG_LOG_FORMAT_ALLOCATION_TEST
#include <SpdlogBackend.h>
#include <gtest/gtest.h>

TEST(LogFormat, OwnsAllInput)
{
    pg::log::LogRecord record;
    std::string message = "plugin text";
    std::string category = "plugin category";
    pg::log::detail::SetRecordContext(record, pg::log::Level::kError, category, std::source_location::current());
    pg::log::detail::FormatRecord(record, "{}", message);
    const auto copy = record;
    message.assign("changed");
    category.assign("changed");
    record = {};
    EXPECT_EQ(copy.Message(), "plugin text");
    EXPECT_EQ(copy.Category(), "plugin category");
    EXPECT_FALSE(copy.File().empty());
}

TEST(LogFormat, AssociatedConversionsAndFormattersAreNeverInvoked)
{
    EXPECT_TRUE(CheckAssociatedConversions());
    EXPECT_EQ(log_format_fixture::gCalls.Normalize, 0U);
    EXPECT_EQ(log_format_fixture::gCalls.Value, 0U);
    EXPECT_EQ(log_format_fixture::gCalls.Formatter, 0U);
}

TEST(LogFormat, LiteralBracesSurviveBackend)
{
    pg::log::LogRecord record;
    pg::log::detail::FormatRecord(record, "{{literal}} {}", 17);
    std::string output;
    pg::log::detail::SpdlogBackend backend{{
                                               .Context = &output,
                                               .Write =
                                                   [](void* context, std::string_view text) noexcept
                                               {
                                                   *static_cast<std::string*>(context) = text;
                                                   return pg::log::detail::OutputOutcome{
                                                       .BytesWritten = text.size(),
                                                       .Complete = true,
                                                   };
                                               },
                                           },
                                           {}};
    const auto outcomes = backend.Emit(pg::log::Level::kInfo, record.Message());
    EXPECT_TRUE(outcomes.front().Complete);
    EXPECT_EQ(output, "{literal} 17");
}

TEST(LogFormat, UnsupportedSpecNeverEntersFmt)
{
    pg::log::LogRecord record;
    record.Failure = pg::log::MakeLogError(pg::log::LogError::kIoFailed);
    pg::log::detail::FormatRecord(record, "{:1000000}", 7);
    EXPECT_TRUE(record.ConstructionFailed);
    EXPECT_EQ(record.Message(), "[log-format-rejected]");
    pg::log::detail::FormatRecord(record, "{0}", 7);
    EXPECT_TRUE(record.ConstructionFailed);
    pg::log::detail::FormatRecord(record, "{:{}}", 7, 8);
    EXPECT_TRUE(record.ConstructionFailed);
    EXPECT_EQ(record.Failure, pg::log::MakeLogError(pg::log::LogError::kIoFailed));
    pg::log::detail::FormatRecord(record, "{:.{}}", 1.0, 100000);
    EXPECT_EQ(record.Message(), "[log-format-rejected]");
}

TEST(LogFormat, Utf8TruncationKeepsErrorIdentity)
{
    const pg::Error suppliedError{pg::log::kLogErrorDomain, 5, {.Source = pg::NativeErrorSourceId{9}, .Value = -17}};
    pg::log::LogRecord record;
    record.Failure = suppliedError;
    const std::string text = std::string(1022, 'a') + "\xe4\xb8\xad";
    pg::log::detail::FormatRecord(record, "{}", std::string_view{text});
    EXPECT_TRUE(record.Truncated);
    EXPECT_EQ(record.Failure, suppliedError);
    EXPECT_EQ(record.Message(), std::string(1022, 'a'));
    pg::log::EncodedLogLine line;
    pg::log::detail::EncodeRecord(record, line);
    EXPECT_LT(line.Text().find("error="), line.Text().find("message="));
    EXPECT_NE(line.Text().find("native=9:-17"), std::string_view::npos);
}

TEST(LogFormat, ControlsAndInvalidUtf8AreBoundedEscapes)
{
    pg::log::LogRecord record;
    pg::log::detail::FormatRecord(record, "{}", std::string_view{"a\n\r\t\0\x1b\xff", 7});
    EXPECT_EQ(record.Message(), "a\\n\\r\\t\\x00\\x1b\\xff");
    EXPECT_FALSE(record.Truncated);
    std::array<char, 1024> controls{};
    controls.fill('\n');
    pg::log::detail::FormatRecord(record, "{}", std::string_view{controls.data(), controls.size()});
    EXPECT_EQ(record.Message().size(), 1024U);
    EXPECT_EQ(record.Message().substr(1022), "\\n");
    EXPECT_TRUE(record.Truncated);
    record = {};
    pg::log::detail::FormatRecord(record, "head\0tail");
    EXPECT_EQ(record.Message(), "head\\x00tail");
}

TEST(LogFormat, UnicodeControlsDoNotCreateExtraLines)
{
    pg::log::LogRecord record;
    pg::log::detail::FormatRecord(record, "{}", std::string_view{"\xc2\x85\xe2\x80\xa8\xe2\x80\xa9"});
    EXPECT_EQ(record.Message(), "\\u0085\\u2028\\u2029");
    EXPECT_FALSE(record.Truncated);
}

TEST(LogFormat, SupportedExtremesAndLongText) { EXPECT_TRUE(RunFormatAllocationCases(std::string(4096, 'a'))); }
TEST(LogFormat, Utf8AtRawOutputBoundaryRemainsComplete)
{
    pg::log::LogRecord record;
    const std::string prefix(1021, 'a');
    pg::log::detail::FormatRecord(record, "{}{}tail", prefix, std::string_view{"\xe4\xb8\xad"});
    EXPECT_TRUE(record.Truncated);
    EXPECT_EQ(record.Message(), prefix + "\xe4\xb8\xad");
    record = {};
    const std::string longer(1022, 'a');
    pg::log::detail::FormatRecord(record, "{}{}", longer, std::string_view{"\xe4\xb8\xad"});
    EXPECT_TRUE(record.Truncated);
    EXPECT_EQ(record.Message(), longer);
}
#endif
