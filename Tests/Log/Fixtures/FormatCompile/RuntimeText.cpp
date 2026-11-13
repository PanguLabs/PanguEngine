#include <LogFormat.h>
void RuntimeText(std::string_view text)
{
    pg::log::LogRecord record;
    pg::log::detail::FormatRecord(record, text, 42);
}
