#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>

namespace pg::module
{
struct DeclarationId
{
    std::array<std::byte, 16> Bytes{};

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return std::ranges::any_of(Bytes, [](std::byte value) { return value != std::byte{0}; });
    }

    auto operator<=>(const DeclarationId&) const noexcept = default;
};
} // namespace pg::module
