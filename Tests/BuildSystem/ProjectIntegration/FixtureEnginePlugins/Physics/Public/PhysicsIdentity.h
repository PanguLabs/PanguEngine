#pragma once

#include <ModuleDeclaration.h>
#include <ModuleIdentity.h>
#include <cstddef>

namespace pg::fixture
{
inline constexpr module::ModuleId kPhysicsId{.Bytes = {std::byte{0x41}}};
inline constexpr module::DeclarationId kPhysicsDeclaration{.Bytes = {std::byte{0x11}}};
} // namespace pg::fixture
