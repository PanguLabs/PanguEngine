#include <Check.h>
#include <Error.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <source_location>
#include <string_view>

#include "Emergency.h"

namespace
{
template <std::size_t Size>
std::uint16_t CopyReason(std::array<char, Size>& destination, std::string_view source, bool& truncated) noexcept
{
    const auto size = std::min(source.size(), destination.size());
    std::copy_n(source.begin(), size, destination.begin());
    truncated = source.size() > size;
    return static_cast<std::uint16_t>(size);
}

template <std::size_t Size>
std::uint16_t CopyLocationText(std::array<char, Size>& destination, const char* source, bool& truncated) noexcept
{
    if (source == nullptr)
    {
        return 0;
    }
    std::uint16_t size{};
    for (auto& character : destination)
    {
        const auto value = *source;
        if (value == '\0')
        {
            return size;
        }
        character = value;
        ++size;
        // 每次只推进已读字符，最多 Size 次；不以 strlen 扫描容量以外的外部文本。
        ++source; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    }
    truncated = true;
    return size;
}
} // namespace

namespace pg
{
[[noreturn]] void Fatal(std::string_view reason, std::optional<Error> error, std::source_location location) noexcept
{
    detail::EmergencySnapshot snapshot;
    snapshot.ReasonSize = CopyReason(snapshot.Reason, reason, snapshot.ReasonTruncated);
    if (error)
    {
        snapshot.HasError = true;
        snapshot.Domain = error->Domain().Value();
        snapshot.Code = error->Code();
        if (const auto native = error->Native())
        {
            snapshot.HasNative = true;
            snapshot.NativeSource = native->Source.Value;
            snapshot.NativeValue = native->Value;
        }
    }
    snapshot.OriginKnown = location.line() != 0;
    if (snapshot.OriginKnown)
    {
        snapshot.Line = location.line();
        snapshot.Column = location.column();
        snapshot.FileSize = CopyLocationText(snapshot.File, location.file_name(), snapshot.FileTruncated);
        snapshot.FunctionSize =
            CopyLocationText(snapshot.Function, location.function_name(), snapshot.FunctionTruncated);
    }
    // 每次调用在自身栈上采集，不分配、不加锁，也不等待首个失败线程；平台边界保留完整现场。
    // 当前没有已证明有界的输出后端，仍不执行普通日志、输出或 Flush。
    detail::EmergencyTerminate(snapshot);
}
} // namespace pg
