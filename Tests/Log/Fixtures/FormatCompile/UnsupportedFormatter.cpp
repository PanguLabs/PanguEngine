#include "UserValue.h"
#include <LogFormat.h>
void UnsupportedFormatter()
{
    pg::log::LogRecord record;
    pg::log::detail::FormatRecord(record, "{}", UserValue{});
}
