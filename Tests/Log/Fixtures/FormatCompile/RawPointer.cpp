#include <LogFormat.h>
void RawPointer(const char* text)
{
    pg::log::LogRecord record;
    pg::log::detail::FormatRecord(record, "{}", text);
}
