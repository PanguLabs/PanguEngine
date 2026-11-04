#pragma once

#include <ModuleDescriptor.h>

#include <cstddef>

namespace pg::fixture
{
inline constexpr module::ModuleId kPhysicsId{.Bytes = {std::byte{0x41}}};
inline constexpr module::ModuleId kGameplayId{.Bytes = {std::byte{0x42}}};
inline constexpr module::ModuleId kEditorExtrasId{.Bytes = {std::byte{0x43}}};

[[nodiscard]] const module::ModuleDefinition* QueryPhysics() noexcept;
[[nodiscard]] const module::ModuleDefinition* QueryGameplay() noexcept;
[[nodiscard]] const module::ModuleDefinition* QueryEditorExtras() noexcept;
[[nodiscard]] module::StaticModuleCatalog GameCatalog() noexcept;
[[nodiscard]] module::StaticModuleCatalog EditorCatalog() noexcept;
} // namespace pg::fixture
