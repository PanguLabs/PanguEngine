#pragma once

#include <ModuleDeclaration.h>
#include <ModuleDescriptor.h>
#include <ModuleIdentity.h>
#include <PanguSharedConsumerExport.h>
#include <PanguSharedProviderExport.h>

#include <cstddef>

namespace pg::module::fixture
{
inline constexpr ModuleId kSharedProviderId{
    .Bytes = {std::byte{0x71}, std::byte{0x03}, std::byte{0x11}, std::byte{0x01}},
};
inline constexpr ModuleId kSharedConsumerId{
    .Bytes = {std::byte{0x72}, std::byte{0x03}, std::byte{0x11}, std::byte{0x02}},
};
inline constexpr DeclarationId kSharedProviderDeclaration{.Bytes = {std::byte{0x71}}};
inline constexpr DeclarationId kSharedConsumerDeclaration{.Bytes = {std::byte{0x72}}};
inline constexpr ModuleId kScopeProviderId{.Bytes = {std::byte{0x76}, std::byte{0x06}}};
inline constexpr ModuleId kScopeConsumerId{.Bytes = {std::byte{0x77}, std::byte{0x06}}};
inline constexpr ModuleId kDiagnosticsCleanupId{.Bytes = {std::byte{0xa3}}};
inline constexpr DeclarationId kScopeContextDeclaration{.Bytes = {std::byte{0x76}, std::byte{1}}};
inline constexpr DeclarationId kScopeWorldDeclaration{.Bytes = {std::byte{0x76}, std::byte{2}}};
inline constexpr DeclarationId kScopePrivateDeclaration{.Bytes = {std::byte{0x76}, std::byte{3}}};
inline constexpr DeclarationId kScopeConsumerDeclaration{.Bytes = {std::byte{0x77}}};
[[nodiscard]] const ModuleDefinition* QueryScopeProvider() noexcept;
[[nodiscard]] const ModuleDefinition* QueryScopeConsumer() noexcept;
[[nodiscard]] const ModuleDefinition* QueryDiagnosticsCleanup() noexcept;
[[nodiscard]] PG_SHARED_PROVIDER_API const ModuleDefinition* QuerySharedProvider() noexcept;
[[nodiscard]] PG_SHARED_PROVIDER_API int SharedProviderValue() noexcept;
[[nodiscard]] PG_SHARED_CONSUMER_API const ModuleDefinition* QuerySharedConsumer() noexcept;
[[nodiscard]] const ModuleDefinition* QueryNullDefinition() noexcept;
} // namespace pg::module::fixture
