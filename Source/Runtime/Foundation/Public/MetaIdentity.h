#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <ranges>

namespace pg::meta
{
struct TypeId
{
    std::array<std::byte, 16> Bytes{};

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return std::ranges::any_of(Bytes, [](std::byte value) { return value != std::byte{0}; });
    }

    auto operator<=>(const TypeId&) const noexcept = default;
};
} // namespace pg::meta
