#include "ModuleDeclaration.h"
#include <RegistrationContext.h>

#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <ModuleIdentity.h>
#include <Result.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace pg::module
{
RegistrationContext::RegistrationContext(RegistrationScope& scope, ModuleId owner, std::uint64_t hostDomain) noexcept
    : Scope(scope)
    , Owner(owner)
    , HostDomain(hostDomain)
{
}

pg::Status RegistrationContext::Declare(DeclarationId id, std::string_view displayName) noexcept
{
    if (Failure)
    {
        return std::unexpected(*Failure);
    }
    const auto reject = [&](ModuleError code) -> pg::Status
    {
        Failure = MakeModuleError(code);
        return std::unexpected(*Failure);
    };
    if (!id.IsValid())
    {
        return reject(ModuleError::kInvalidDeclaration);
    }
    if (displayName.size() > kMaxModuleTextBytes || Pending.size() >= kMaxRegistrationDeclarations ||
        Scope.Published.size() + Pending.size() >= kMaxScopeDeclarations)
    {
        return reject(ModuleError::kLimitExceeded);
    }
    if (std::ranges::find(Pending, id, &RegisteredDeclaration::Id) != Pending.end() ||
        std::ranges::find(Scope.Published, id, &RegisteredDeclaration::Id) != Scope.Published.end())
    {
        return reject(ModuleError::kDeclarationConflict);
    }
    Pending.push_back({.Owner = Owner, .Id = id, .DisplayName = std::string(displayName), .HostDomain = HostDomain});
    return {};
}

pg::Status RegistrationContext::DeclareFactory(DeclarationId id, pg::service::Scope kind, ScopeFactory factory,
                                               std::span<const ScopeDependency> dependencies,
                                               std::string_view displayName) noexcept
{
    if (Failure)
    {
        return std::unexpected(*Failure);
    }
    const auto reject = [&](ModuleError error) -> pg::Status
    {
        Failure = MakeModuleError(error);
        return std::unexpected(*Failure);
    };
    const auto validKind = [](pg::service::Scope value)
    {
        return value == pg::service::Scope::kHost || value == pg::service::Scope::kContext ||
               value == pg::service::Scope::kWorld;
    };
    if (!factory)
    {
        return reject(ModuleError::kInvalidDeclaration);
    }
    if (!validKind(kind))
    {
        return reject(ModuleError::kInvalidScope);
    }
    if (dependencies.size() > 64)
    {
        return reject(ModuleError::kLimitExceeded);
    }
    for (std::size_t index = 0; index < dependencies.size(); ++index)
    {
        const auto& dependency = dependencies.subspan(index).front();
        if (!validKind(dependency.Scope) || static_cast<unsigned>(dependency.Scope) > static_cast<unsigned>(kind))
        {
            return reject(ModuleError::kInvalidScope);
        }
        if (!dependency.Id.IsValid() || dependency.Id == id ||
            std::ranges::any_of(dependencies.first(index),
                                [&](const auto& previous) { return previous.Id == dependency.Id; }))
        {
            return reject(ModuleError::kInvalidDependency);
        }
    }
    if (const auto declared = Declare(id, displayName); !declared)
    {
        return declared;
    }
    auto recipe = std::make_unique<RegistrationScope::Recipe>();
    recipe->Owner = Owner;
    recipe->Id = id;
    recipe->HostDomain = HostDomain;
    recipe->Kind = kind;
    recipe->Factory = std::move(factory);
    recipe->Dependencies.assign(dependencies.begin(), dependencies.end());
    PendingRecipes.push_back(std::move(recipe));
    return {};
}
} // namespace pg::module
