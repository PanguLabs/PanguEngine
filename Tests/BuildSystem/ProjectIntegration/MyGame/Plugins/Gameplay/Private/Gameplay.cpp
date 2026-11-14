#include "FixtureModules.h"
#include "GameplayModule.h"
#include "GameplayService.h"
#include "ModuleDeclaration.h"
#include "ModuleDescriptor.h"
#include "Result.h"

#include <RegistrationContext.h>
#include <ScopeBuildContext.h>
#include <memory>

#include <ScopeFactory.h>
#include <ScopeIdentity.h>
#include <array>
#include <expected>

namespace pg::fixture
{
namespace
{
[[nodiscard]] pg::Status RegisterGameplay(module::RegistrationContext& context) noexcept
{
    return context.DeclareFactory(kGameplayDeclaration, service::Scope::kContext,
                                  module::ScopeFactory(
                                      [](module::ScopeBuildContext& build) noexcept -> Status
                                      {
                                          const auto value = build.Own(std::make_unique<GameplaySession>());
                                          return value ? Status{} : Status{std::unexpected(value.error())};
                                      }),
                                  {}, "Gameplay");
}
} // namespace

const module::ModuleDefinition* QueryGameplay() noexcept
{
    static constexpr std::array kDependencies{module::ModuleDependency{.Id = kPhysicsId}};
    static const module::ModuleDefinition kDefinition{
        .Descriptor =
            {
                .Id = kGameplayId,
                .Dependencies = kDependencies,
                .DisplayName = "Gameplay",
                .BuildIdentity = "fixture",
            },
        .RegisterModule = RegisterGameplay,
    };
    return &kDefinition;
}
} // namespace pg::fixture
