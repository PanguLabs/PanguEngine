#pragma once
#include <fmt/format.h>

#include <string>

struct UserValue
{
};

namespace log_format_fixture
{
struct AdlCalls
{
    unsigned int Normalize{};
    unsigned int Value{};
    unsigned int Formatter{};
};
// 两个独立测试进程均串行执行；这些计数仅观测被禁止的用户扩展调用。
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline AdlCalls gCalls;

// 固定 int 底层类型，用 Positive 的类型断言验证转换边界，而非为业务存储选择枚举宽度。
// NOLINTNEXTLINE(performance-enum-size)
enum class StringCode : int
{
    kValue = 37,
};
// NOLINTNEXTLINE(performance-enum-size) 与 StringCode 一样保留被审阅复现的 int 归一化契约。
enum class FormatterCode : int
{
    kValue = 43,
};

inline std::string NormalizeLogArgument(StringCode /*value*/, bool& /*truncated*/)
{
    ++gCalls.Normalize;
    std::string text(4096, 'x');
    return text;
}
inline UserValue NormalizeLogArgument(FormatterCode /*value*/, bool& /*truncated*/) noexcept
{
    ++gCalls.Normalize;
    return {};
}
} // namespace log_format_fixture

inline UserValue LogFmtValue(UserValue value) noexcept
{
    ++log_format_fixture::gCalls.Value;
    return value;
}

template <>
struct fmt::formatter<UserValue> : fmt::formatter<int>
{
    auto format(UserValue /*value*/, fmt::format_context& context) const
    {
        ++log_format_fixture::gCalls.Formatter;
        return fmt::formatter<int>::format(42, context);
    }
};
