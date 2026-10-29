#pragma once

#include <compare>
#include <cstdint>

namespace pg::service
{
enum class Scope : std::uint8_t
{
    kHost,
    kContext,
    kWorld,
};
} // namespace pg::service

namespace pg::module
{
struct ScopeHandle
{
    std::uint64_t Domain{};
    std::uint32_t Slot{};
    std::uint32_t Generation{};

    [[nodiscard]] constexpr bool IsValid() const noexcept { return Domain != 0 && Slot != 0 && Generation != 0; }
    auto operator<=>(const ScopeHandle&) const noexcept = default;
};
} // namespace pg::module
