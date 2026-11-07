#pragma once

#include <Error.h>
#include <cstdint>

namespace pg::engine
{
inline constexpr auto kHostErrorDomain = pg::ErrorDomainId::Constant<0x70672e686f7374ULL>();
enum class HostError : std::uint8_t
{
    kInvalidState = 1,
    kBusy = 2,
};
[[nodiscard]] constexpr pg::Error MakeHostError(HostError code) noexcept
{
    return {kHostErrorDomain, static_cast<std::uint32_t>(code)};
}
} // namespace pg::engine
