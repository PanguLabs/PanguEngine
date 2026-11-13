#include <LogFormat.h>
void LongDouble()
{
    pg::log::LogRecord record;
    pg::log::detail::FormatRecord(record, "{}", 1.0L);
}
