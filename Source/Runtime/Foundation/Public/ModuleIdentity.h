#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <ranges>

namespace pg::module
{
struct ModuleId
{
    std::array<std::byte, 16> Bytes{};

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return std::ranges::any_of(Bytes, [](std::byte value) { return value != std::byte{0}; });
    }

    auto operator<=>(const ModuleId&) const noexcept = default;
};

struct ModuleVersion
{
    std::uint32_t Major{};
    std::uint32_t Minor{};
    std::uint32_t Patch{};

    constexpr auto operator<=>(const ModuleVersion&) const noexcept = default;
};

// Exactly 32 lowercase hex digits in stored byte order; no terminator or allocation.
[[nodiscard]] constexpr std::array<char, 32> FormatModuleId(const ModuleId& id) noexcept
{
    constexpr auto kHexDigit = [](unsigned int digit) -> char
    { return static_cast<char>(digit < 10U ? '0' + digit : 'a' + (digit - 10U)); };

    std::array<char, 32> result{};
    auto output = result.begin();
    for (const auto byte : id.Bytes)
    {
        const auto value = static_cast<unsigned int>(std::to_integer<std::uint8_t>(byte));
        *output++ = kHexDigit(value / 16U);
        *output++ = kHexDigit(value % 16U);
    }
    return result;
}
} // namespace pg::module
