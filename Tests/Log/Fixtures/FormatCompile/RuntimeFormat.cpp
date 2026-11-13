#include <LogFormat.h>
void RuntimeFormat(std::string_view text)
{
    pg::log::LogRecord record;
    pg::log::detail::FormatRecord(record, fmt::runtime(text), 42);
}
