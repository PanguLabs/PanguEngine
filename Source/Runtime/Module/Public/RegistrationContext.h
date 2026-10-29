#pragma once

#include <ModuleDeclaration.h>
#include <ModuleIdentity.h>
#include <Result.h>
#include <ScopeFactory.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pg::module
{
inline constexpr std::size_t kMaxRegistrationDeclarations = 256;
inline constexpr std::size_t kMaxScopeDeclarations = 4096;

struct RegisteredDeclaration
{
    ModuleId Owner{};
    DeclarationId Id{};
    std::string DisplayName;
    std::uint64_t HostDomain{};
};

class StaticModuleHost;
namespace detail
{
class ScopeRuntime;
}

// Host declaration domain; runtime Host/Context/World instances have separate Owners.
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

    // Borrow expires on registration, declaration revocation (Unload/PollCleanup/
    // Shutdown/Host destruction), or declaration-domain destruction.
    [[nodiscard]] std::span<const RegisteredDeclaration> Declarations() const noexcept { return Published; }

private:
    friend class RegistrationContext;
    friend class StaticModuleHost;
    friend class detail::ScopeRuntime;
    struct Recipe
    {
        ModuleId Owner{};
        DeclarationId Id{};
        std::uint64_t HostDomain{};
        pg::service::Scope Kind{};
        ScopeFactory Factory;
        std::vector<ScopeDependency> Dependencies;
    };
    std::vector<RegisteredDeclaration> Published;
    std::vector<std::unique_ptr<Recipe>> Recipes;
    bool Registering{};
};

// Callback-local borrowed transaction. Do not retain this object or its address.
// Declares metadata and repeatable factories; it does not start services or work.
class RegistrationContext final
{
public:
    ~RegistrationContext() = default;
    RegistrationContext(const RegistrationContext&) = delete;
    RegistrationContext& operator=(const RegistrationContext&) = delete;
    RegistrationContext(RegistrationContext&&) = delete;
    RegistrationContext& operator=(RegistrationContext&&) = delete;

    [[nodiscard]] pg::Status Declare(DeclarationId id, std::string_view displayName) noexcept;
    [[nodiscard]] pg::Status DeclareFactory(DeclarationId id, pg::service::Scope kind, ScopeFactory factory,
                                            std::span<const ScopeDependency> dependencies = {},
                                            std::string_view displayName = {}) noexcept;

private:
    friend class StaticModuleHost;
    RegistrationContext(RegistrationScope& scope, ModuleId owner, std::uint64_t hostDomain = 0) noexcept;

    RegistrationScope& Scope;
    ModuleId Owner;
    std::uint64_t HostDomain;
    std::vector<RegisteredDeclaration> Pending;
    std::vector<std::unique_ptr<RegistrationScope::Recipe>> PendingRecipes;
    std::optional<pg::Error> Failure;
};
} // namespace pg::module
