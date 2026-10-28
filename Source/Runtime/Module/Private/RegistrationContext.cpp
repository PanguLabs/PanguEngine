#include <RegistrationContext.h>

#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <ModuleIdentity.h>
#include <Result.h>

#include <algorithm>
#include <expected>
#include <string>
#include <string_view>

namespace pg::module
{
RegistrationContext::RegistrationContext(RegistrationScope& scope, ModuleId owner) noexcept
    : Scope(scope)
    , Owner(owner)
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
    Pending.push_back({.Owner = Owner, .Id = id, .DisplayName = std::string(displayName)});
    return {};
}
} // namespace pg::module
