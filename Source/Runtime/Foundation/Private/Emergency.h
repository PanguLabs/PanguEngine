#pragma once

#include <array>
#include <cstdint>

namespace pg::detail
{
// 单次 Fatal 独占的拥有型现场；不保存调用方/Plugin 指针，也不依赖全局首次失败槽。
// 文本按长度读取，不要求尾零；达到未知长度文本的容量边界时保守标记截断。
struct EmergencySnapshot
{
    std::array<char, 128> Reason{};
    std::array<char, 160> File{};
    std::array<char, 96> Function{};
    std::uint64_t Domain{};
    std::uint64_t NativeSource{};
    std::int64_t NativeValue{};
    std::uint32_t Code{};
    std::uint32_t Line{};
    std::uint32_t Column{};
    std::uint16_t ReasonSize{};
    std::uint16_t FileSize{};
    std::uint16_t FunctionSize{};
    bool HasError{};
    bool HasNative{};
    bool OriginKnown{};
    bool ReasonTruncated{};
    bool FileTruncated{};
    bool FunctionTruncated{};
};
static_assert(sizeof(EmergencySnapshot) <= 512);

[[noreturn]] void EmergencyTerminate(const EmergencySnapshot& snapshot) noexcept;
} // namespace pg::detail
