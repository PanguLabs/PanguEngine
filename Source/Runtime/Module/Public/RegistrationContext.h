#pragma once

#include <ModuleIdentity.h>
#include <Result.h>

#include <array>
#include <cstddef>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pg::module
{
inline constexpr std::size_t kMaxRegistrationDeclarations = 256;
inline constexpr std::size_t kMaxScopeDeclarations = 4096;

struct DeclarationId
{
    std::array<std::byte, 16> Bytes{};

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return std::ranges::any_of(Bytes, [](std::byte value) { return value != std::byte{0}; });
    }

    auto operator<=>(const DeclarationId&) const noexcept = default;
};

struct RegisteredDeclaration
{
    ModuleId Owner{};
    DeclarationId Id{};
    std::string DisplayName;
};

class StaticModuleHost;

// One concrete owner, not a Host/Context/World kind or a global registry.
// Calls must be externally serialized. OOM uses the standard no-exception termination policy.
class RegistrationScope final
{
public:
    RegistrationScope() = default;
    ~RegistrationScope() = default;
    RegistrationScope(const RegistrationScope&) = delete;
    RegistrationScope& operator=(const RegistrationScope&) = delete;
    RegistrationScope(RegistrationScope&&) = delete;
    RegistrationScope& operator=(RegistrationScope&&) = delete;

    // Borrow expires at the next successful registration or Scope destruction.
    [[nodiscard]] std::span<const RegisteredDeclaration> Declarations() const noexcept { return Published; }

private:
    friend class RegistrationContext;
    friend class StaticModuleHost;
    std::vector<RegisteredDeclaration> Published;
    bool Registering{};
};

// Callback-local borrowed transaction. Do not retain this object or its address.
// Only declaration metadata is supported; this API does not start services or work.
class RegistrationContext final
{
public:
    ~RegistrationContext() = default;
    RegistrationContext(const RegistrationContext&) = delete;
    RegistrationContext& operator=(const RegistrationContext&) = delete;
    RegistrationContext(RegistrationContext&&) = delete;
    RegistrationContext& operator=(RegistrationContext&&) = delete;

    [[nodiscard]] pg::Status Declare(DeclarationId id, std::string_view displayName) noexcept;

private:
    friend class StaticModuleHost;
    RegistrationContext(RegistrationScope& scope, ModuleId owner) noexcept;

    RegistrationScope& Scope;
    ModuleId Owner;
    std::vector<RegisteredDeclaration> Pending;
    std::optional<pg::Error> Failure;
};
} // namespace pg::module
