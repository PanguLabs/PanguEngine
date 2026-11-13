#include <LogFormat.h>
void RuntimeFormatObject(std::string_view text)
{
    pg::log::LogRecord record;
    fmt::format_string<int> format = fmt::runtime(text);
    pg::log::detail::FormatRecord(record, format, 42);
}
