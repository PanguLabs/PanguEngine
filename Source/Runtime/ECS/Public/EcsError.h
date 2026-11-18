#pragma once

#include <Error.h>
#include <cstdint>

namespace pg::ecs
{
inline constexpr auto kEcsErrorDomain = ErrorDomainId::Constant<0x70672e656373ULL>();

enum class EcsError : std::uint8_t
{
    kInvalidDeltaTime = 1,
    kWorldStopping = 2,
};

[[nodiscard]] constexpr Error MakeEcsError(EcsError code) noexcept
{
    return {kEcsErrorDomain, static_cast<std::uint32_t>(code)};
}
} // namespace pg::ecs
