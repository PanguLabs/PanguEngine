#include "UserValue.h"
#include <LogFormat.h>
#include <concepts>
static_assert(fmt::is_formattable<UserValue>::value);
static_assert(std::same_as<pg::log::detail::LogFmtArgument<log_format_fixture::StringCode>, int>);
static_assert(std::same_as<pg::log::detail::LogFmtArgument<log_format_fixture::FormatterCode>, int>);
void Positive()
{
    pg::log::LogRecord record;
    pg::log::detail::FormatRecord(record, "{} {{ok}}", 42);
    pg::log::detail::FormatRecord(record, "{} {}", log_format_fixture::StringCode::kValue,
                                  log_format_fixture::FormatterCode::kValue);
    std::array<char, 8> output;
    fmt::format_to(output.begin(), "{}", UserValue{});
}
