#include "FixtureModules.h"
#include "ModuleDeclaration.h"
#include "ModuleDescriptor.h"
#include "PhysicsModule.h"
#include "Result.h"

#include <ModuleBuiltin.h>
#include <RegistrationContext.h>
#include <array>
#include <cstddef>

namespace pg::fixture
{
namespace
{
[[nodiscard]] pg::Status RegisterPhysics(module::RegistrationContext& context) noexcept
{
    return context.Declare(module::DeclarationId{.Bytes = {std::byte{0x11}}}, "Physics");
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
