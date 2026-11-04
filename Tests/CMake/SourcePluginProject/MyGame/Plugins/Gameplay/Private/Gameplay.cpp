#include "FixtureModules.h"
#include "ModuleDeclaration.h"
#include "ModuleDescriptor.h"
#include "Result.h"

#include <RegistrationContext.h>

#include <array>
#include <cstddef>

namespace pg::fixture
{
namespace
{
[[nodiscard]] pg::Status RegisterGameplay(module::RegistrationContext& context) noexcept
{
    return context.Declare(module::DeclarationId{.Bytes = {std::byte{0x12}}}, "Gameplay");
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
