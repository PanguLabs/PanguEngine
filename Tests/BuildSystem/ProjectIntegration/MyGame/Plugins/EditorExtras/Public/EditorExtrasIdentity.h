#pragma once

#include <ModuleDeclaration.h>
#include <ModuleIdentity.h>
#include <cstddef>

namespace pg::fixture
{
inline constexpr module::ModuleId kEditorExtrasId{.Bytes = {std::byte{0x43}}};
inline constexpr module::DeclarationId kEditorDeclaration{.Bytes = {std::byte{0x13}}};
} // namespace pg::fixture
