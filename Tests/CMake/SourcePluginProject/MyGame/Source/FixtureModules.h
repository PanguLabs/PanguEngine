#pragma once

#include <ModuleDeclaration.h>
#include <ModuleDescriptor.h>

#include <cstddef>

namespace pg::fixture
{
inline constexpr module::ModuleId kPhysicsId{.Bytes = {std::byte{0x41}}};
inline constexpr module::ModuleId kGameplayId{.Bytes = {std::byte{0x42}}};
inline constexpr module::ModuleId kEditorExtrasId{.Bytes = {std::byte{0x43}}};
inline constexpr module::ModuleId kSharedProbeId{.Bytes = {std::byte{0x44}}};
inline constexpr module::DeclarationId kPhysicsDeclaration{.Bytes = {std::byte{0x11}}};
inline constexpr module::DeclarationId kGameplayDeclaration{.Bytes = {std::byte{0x12}}};
inline constexpr module::DeclarationId kEditorDeclaration{.Bytes = {std::byte{0x13}}};

[[nodiscard]] module::StaticModuleCatalog GameCatalog() noexcept;
[[nodiscard]] module::StaticModuleCatalog EditorCatalog() noexcept;
} // namespace pg::fixture
