#pragma once

#include <ModuleDeclaration.h>
#include <ModuleIdentity.h>
#include <cstddef>

namespace pg::fixture
{
inline constexpr module::ModuleId kGameplayId{.Bytes = {std::byte{0x42}}};
inline constexpr module::DeclarationId kGameplayDeclaration{.Bytes = {std::byte{0x12}}};
} // namespace pg::fixture
