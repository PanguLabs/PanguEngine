#include "FixtureModules.h"

#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>

#include <array>
#include <expected>
#include <memory>
#include <utility>
#include <vector>

namespace pg::module::fixture
{
namespace
{
[[nodiscard]] pg::Status Validate(FixtureValue& value) noexcept
{
    return value.Values.size() == 1 && value.Values.front() == 33
               ? pg::Status{}
               : pg::Status{std::unexpected(MakeModuleError(ModuleError::kInvalidState))};
}
[[nodiscard]] pg::Status Register(RegistrationContext& context) noexcept
{
    constexpr std::array kScopeDependencies{
        ScopeDependency{.Id = kSecondDeclaration, .Scope = pg::service::Scope::kHost},
    };
    return context.DeclareFactory(
        kFirstDeclaration, pg::service::Scope::kContext,
        ScopeFactory(
            [](ScopeBuildContext& build) noexcept -> pg::Status
            {
                const auto provider = build.Dependency<FixtureValue>(kSecondDeclaration);
                if (!provider)
                {
                    return std::unexpected(provider.error());
                }
                auto object = std::make_unique<FixtureValue>(std::vector<int>{(*provider)->Values.front() + 11});
                const auto access = build.Own(std::move(object), ScopeHooks<FixtureValue>{.Start = Validate});
                return access ? pg::Status{} : std::unexpected(access.error());
            }),
        kScopeDependencies, "first");
}
constexpr std::array kDependencies{ModuleDependency{.Id = kSecondId}};
constexpr ModuleDefinition kDefinition{
    .Descriptor = {.Id = kFirstId, .Dependencies = kDependencies, .DisplayName = "First", .BuildIdentity = {}},
    .RegisterModule = Register,
};
} // namespace

const ModuleDefinition* QueryFirst() noexcept { return &kDefinition; }
} // namespace pg::module::fixture
