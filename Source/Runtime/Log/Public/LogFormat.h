#pragma once

#include <LogRecord.h>
#include <PanguLogExport.h>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <ranges>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace pg::log::detail
{
inline constexpr std::size_t kMaxLogFormatBytes = 4096;
inline constexpr std::size_t kMaxLogArguments = 16;
// 默认浮点路径限定为 fmt 的 IEEE binary32/binary64 Dragonbox 分支。
static_assert(std::numeric_limits<float>::is_iec559 && sizeof(float) == 4);
static_assert(std::numeric_limits<double>::is_iec559 && sizeof(double) == 8);

// fmt 的输出迭代器仅写固定 span；超出后只计截断，不增长缓冲。
struct LogFormatBuffer
{
    std::span<char> Bytes;
    std::size_t Used{};
    bool Truncated{};
    void Put(char value) noexcept
    {
        if (Used < Bytes.size())
        {
            Bytes.subspan(Used, 1).front() = value;
            ++Used;
        }
        else
        {
            Truncated = true;
        }
    }
    [[nodiscard]] std::string_view Text() const noexcept { return {Bytes.data(), Used}; }
};
class LogOutputIterator final
{
public:
    using iterator_category = std::output_iterator_tag;
    using value_type = char;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = void;

    explicit LogOutputIterator(LogFormatBuffer& buffer) noexcept
        : Buffer(&buffer)
    {
    }
    LogOutputIterator& operator=(char value) noexcept
    {
        Buffer->Put(value);
        return *this;
    }
    LogOutputIterator& operator*() noexcept { return *this; }
    LogOutputIterator& operator++() noexcept { return *this; }
    LogOutputIterator operator++(int) noexcept { return *this; }

private:
    LogFormatBuffer* Buffer;
};

struct LogIdentityText
{
    std::array<char, 128> Bytes{};
    std::size_t Used{};
};

[[nodiscard]] PG_LOG_API bool IsSupportedLogFormat(std::string_view format, std::size_t arguments) noexcept;
[[nodiscard]] PG_LOG_API std::string_view BoundLogText(std::string_view text, bool& truncated) noexcept;
[[nodiscard]] PG_LOG_API LogIdentityText NormalizeLogArgument(const module::ModuleId& value, bool& truncated) noexcept;
[[nodiscard]] PG_LOG_API LogIdentityText NormalizeLogArgument(Error value, bool& truncated) noexcept;
PG_LOG_API void FinishLogMessage(LogRecord& record, const LogFormatBuffer& buffer) noexcept;
PG_LOG_API void RejectLogFormat(LogRecord& record) noexcept;
PG_LOG_API void SetRecordContext(LogRecord& record, Level level, std::string_view category,
                                 std::source_location location = std::source_location::current(),
                                 std::optional<Error> failure = {}, LogTags tags = {}) noexcept;
PG_LOG_API void EncodeRecord(const LogRecord& record, EncodedLogLine& line) noexcept;

template <typename T>
concept LogScalar = std::same_as<T, bool> || std::same_as<T, char> || std::same_as<T, signed char> ||
                    std::same_as<T, unsigned char> || std::same_as<T, short> || std::same_as<T, unsigned short> ||
                    std::same_as<T, int> || std::same_as<T, unsigned int> || std::same_as<T, long> ||
                    std::same_as<T, unsigned long> || std::same_as<T, long long> ||
                    std::same_as<T, unsigned long long> || std::same_as<T, float> || std::same_as<T, double>;

template <LogScalar T>
[[nodiscard]] constexpr T NormalizeLogArgument(T value, bool& /*truncated*/) noexcept
{
    return value;
}
template <typename T>
    requires(std::is_enum_v<T> && LogScalar<std::underlying_type_t<T>>)
[[nodiscard]] constexpr auto NormalizeLogArgument(T value, bool& /*truncated*/) noexcept
{
    return static_cast<std::underlying_type_t<T>>(value);
}
[[nodiscard]] inline std::string_view NormalizeLogArgument(std::string_view value, bool& truncated) noexcept
{
    return BoundLogText(value, truncated);
}
[[nodiscard]] inline std::string_view NormalizeLogArgument(const std::string& value, bool& truncated) noexcept
{
    return BoundLogText(value, truncated);
}
template <std::size_t Size>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays) 用数组引用保留调用方的已知边界。
[[nodiscard]] std::string_view NormalizeLogArgument(const char (&value)[Size], bool& truncated) noexcept
{
    const auto bytes = std::span{value};
    const auto length = std::min(Size, kLogMessageCapacity + 1);
    const auto bounded = bytes.first(length);
    const auto terminator = std::ranges::find(bounded, '\0');
    return BoundLogText({bounded.data(), static_cast<std::size_t>(terminator - bounded.begin())}, truncated);
}

template <typename T>
[[nodiscard]] constexpr T LogFmtValue(T value) noexcept
{
    return value;
}
[[nodiscard]] inline std::string_view LogFmtValue(const LogIdentityText& value) noexcept
{
    return {value.Bytes.data(), value.Used};
}
// 不借用任意隐式转换；裸指针与用户 formatter 即使合法也不能进入此集合。
template <typename T>
concept LogArgument =
    LogScalar<std::remove_cvref_t<T>> || std::is_enum_v<std::remove_cvref_t<T>> ||
    std::same_as<std::remove_cvref_t<T>, std::string> || std::same_as<std::remove_cvref_t<T>, std::string_view> ||
    std::same_as<std::remove_cvref_t<T>, module::ModuleId> || std::same_as<std::remove_cvref_t<T>, Error> ||
    (std::is_array_v<std::remove_reference_t<T>> &&
     std::same_as<std::remove_cv_t<std::remove_extent_t<std::remove_reference_t<T>>>, char>);

template <typename T>
// 推导与执行都只使用封闭的内部转换，不能被参数关联命名空间的 ADL 重载替换。
using LogFmtArgument = decltype(::pg::log::detail::LogFmtValue(
    ::pg::log::detail::NormalizeLogArgument(std::declval<T>(), std::declval<bool&>())));

// fmt::format_string 也接受 fmt::runtime；外层只接编译期字符数组，封住该隐式旁路。
template <typename... Args>
class LogFormatString final
{
public:
    template <std::size_t Size>
    // 字面量隐式转换是前端调用协议；保留数组边界以排除运行期指针。
    // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor,misc-explicit-constructor,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    consteval LogFormatString(const char (&literal)[Size])
        : CheckedFormat(literal)
    {
    }
    [[nodiscard]] auto Get() const noexcept { return CheckedFormat; }

private:
    fmt::format_string<Args...> CheckedFormat;
};

template <typename... Args>
    requires((LogArgument<Args> && ...) && sizeof...(Args) <= kMaxLogArguments)
void FormatRecord(LogRecord& record, std::type_identity_t<LogFormatString<LogFmtArgument<Args>...>> format,
                  Args&&... args) noexcept
{
    const auto text = format.Get().get();
    if (!IsSupportedLogFormat({text.data(), text.size()}, sizeof...(Args)))
    {
        RejectLogFormat(record);
        return;
    }
    // tuple 拥有身份编码临时值；string_view 只在同步 fmt 调用内借用，绝不存入记录。
    const auto values =
        std::tuple{::pg::log::detail::NormalizeLogArgument(std::forward<Args>(args), record.Truncated)...};
    std::array<char, kLogMessageCapacity> bytes{};
    LogFormatBuffer buffer{.Bytes = bytes};
    std::apply([&](const auto&... value)
               { fmt::format_to(LogOutputIterator{buffer}, format.Get(), ::pg::log::detail::LogFmtValue(value)...); },
               values);
    FinishLogMessage(record, buffer);
}
} // namespace pg::log::detail
