#pragma once

#include <ModuleIdentity.h>
#include <cstddef>

namespace pg::fixture
{
inline constexpr module::ModuleId kSharedProbeId{.Bytes = {std::byte{0x44}}};
} // namespace pg::fixture
