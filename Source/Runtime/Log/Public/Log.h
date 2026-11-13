#pragma once

#include <Check.h>
#include <LogFormat.h>
#include <LogWriter.h>

// 宏只创建同步借用闭包；所有文本、参数及位置信息都在提交保护内复制。
// NOLINTBEGIN(cppcoreguidelines-macro-usage): 参数惰性求值必须保留调用点表达式。
#define PG_LOG(level, writer, category, formatLiteral, ...)                                                            \
    (                                                                                                                  \
        [&, pgLogLocation = std::source_location::current()]() noexcept                                                \
        {                                                                                                              \
            const auto pgLogLevel = (level);                                                                           \
            const auto& pgLogWriter = (writer);                                                                        \
            if (static_cast<unsigned int>(pgLogLevel) < PG_LOG_COMPILED_MIN_LEVEL)                                     \
            {                                                                                                          \
                return ::pg::log::SubmitResult::kFiltered;                                                             \
            }                                                                                                          \
            return ::pg::log::detail::SubmitLazy(                                                                      \
                pgLogWriter, pgLogLevel,                                                                               \
                [&](::pg::log::LogRecord& pgLogRecord) noexcept                                                        \
                {                                                                                                      \
                    ::pg::log::detail::SetRecordContext(pgLogRecord, pgLogLevel, (category), pgLogLocation);           \
                    ::pg::log::detail::FormatRecord(pgLogRecord, formatLiteral __VA_OPT__(, ) __VA_ARGS__);            \
                });                                                                                                    \
        }())
#define PG_LOG_TRACE(writer, category, formatLiteral, ...)                                                             \
    PG_LOG(::pg::log::Level::kTrace, writer, category, formatLiteral __VA_OPT__(, ) __VA_ARGS__)
#define PG_LOG_DEBUG(writer, category, formatLiteral, ...)                                                             \
    PG_LOG(::pg::log::Level::kDebug, writer, category, formatLiteral __VA_OPT__(, ) __VA_ARGS__)
#define PG_LOG_INFO(writer, category, formatLiteral, ...)                                                              \
    PG_LOG(::pg::log::Level::kInfo, writer, category, formatLiteral __VA_OPT__(, ) __VA_ARGS__)
#define PG_LOG_WARNING(writer, category, formatLiteral, ...)                                                           \
    PG_LOG(::pg::log::Level::kWarning, writer, category, formatLiteral __VA_OPT__(, ) __VA_ARGS__)
#define PG_LOG_ERROR(writer, category, formatLiteral, ...)                                                             \
    PG_LOG(::pg::log::Level::kError, writer, category, formatLiteral __VA_OPT__(, ) __VA_ARGS__)
// NOLINTEND(cppcoreguidelines-macro-usage)
