#include "ModuleDeclaration.h"
#include "ModuleDescriptor.h"
#include "PhysicsIdentity.h"
#include "PhysicsModule.h"
#include "Result.h"

#include <ModuleBuiltin.h>
#include <RegistrationContext.h>
#include <array>

namespace pg::fixture
{
namespace
{
[[nodiscard]] pg::Status RegisterPhysics(module::RegistrationContext& context) noexcept
{
    return context.Declare(kPhysicsDeclaration, "Physics");
}
} // namespace

const module::ModuleDefinition* QueryPhysics() noexcept
{
    static constexpr std::array kDependencies{
        module::ModuleDependency{
            .Id = module::kModuleRuntimeModuleId,
            .Version = {.Major = 1, .Minor = 0, .Patch = 0},
        },
    };
    static const module::ModuleDefinition kDefinition{
        .Descriptor =
            {
                .Id = kPhysicsId,
                .Dependencies = kDependencies,
                .DisplayName = "Physics",
                .BuildIdentity = "fixture",
            },
        .RegisterModule = RegisterPhysics,
    };
    return &kDefinition;
}
} // namespace pg::fixture
