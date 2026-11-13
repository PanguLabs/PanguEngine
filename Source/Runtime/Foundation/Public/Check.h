#pragma once

#include <Error.h>
#include <PanguFoundationExport.h>

#include <optional>
#include <source_location>
#include <string_view>

#ifndef PG_ENABLE_ASSERTS
#error PG_ENABLE_ASSERTS must come from the PanguFoundation target
#endif
#ifndef PG_LOG_COMPILED_MIN_LEVEL
#error PG_LOG_COMPILED_MIN_LEVEL must come from the PanguFoundation target
#endif
#if PG_ENABLE_ASSERTS != 0 && PG_ENABLE_ASSERTS != 1
#error PG_ENABLE_ASSERTS must be 0 or 1
#endif
#if PG_LOG_COMPILED_MIN_LEVEL < 0 || PG_LOG_COMPILED_MIN_LEVEL > 4
#error PG_LOG_COMPILED_MIN_LEVEL must be between 0 and 4
#endif

namespace pg
{
// 仅用于普通线程的不可恢复失败；固定预算采集现场，应急输出可省略文本，不执行普通日志或清理。
[[noreturn]] PG_FOUNDATION_API void Fatal(std::string_view reason, std::optional<Error> error = {},
                                          std::source_location location = std::source_location::current()) noexcept;
} // namespace pg

// 条件只求值一次，消息仅在失败分支求值；表达式宏也可安全用于 if/else 的单语句分支。
// NOLINTBEGIN(cppcoreguidelines-macro-usage): 惰性求值需要宏保留调用点表达式。
#define PG_CHECK(condition, message) (static_cast<bool>(condition) ? static_cast<void>(0) : ::pg::Fatal(message))
#if PG_ENABLE_ASSERTS
#define PG_ASSERT(condition, message) PG_CHECK(condition, message)
#else
#define PG_ASSERT(condition, message) static_cast<void>(0)
#endif
// NOLINTEND(cppcoreguidelines-macro-usage)
