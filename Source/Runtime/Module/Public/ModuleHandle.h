#pragma once
#include <compare>
#include <cstdint>
namespace pg::module
{
struct ModuleHandle
{
    std::uint64_t Domain{};
    std::uint32_t Slot{};
    std::uint64_t Generation{};
    [[nodiscard]] constexpr bool IsValid() const noexcept { return Domain != 0 && Slot != 0 && Generation != 0; }
    auto operator<=>(const ModuleHandle&) const noexcept = default;
};
} // namespace pg::module
