#include "SharedProbe.h"
#include "FixtureModules.h"
#include <ModuleBuiltin.h>
#include <ModuleDeclaration.h>
#include <ModuleDescriptor.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <array>
#include <cstddef>

namespace pg::fixture
{
int SharedProbeValue() noexcept { return 1; }
namespace
{
[[nodiscard]] pg::Status RegisterSharedProbe(module::RegistrationContext& context) noexcept
{
    return context.Declare(module::DeclarationId{.Bytes = {std::byte{0x14}}}, "SharedProbe");
}
} // namespace
const module::ModuleDefinition* QuerySharedProbe() noexcept
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
                .Id = kSharedProbeId,
                .Dependencies = kDependencies,
                .DisplayName = "SharedProbe",
                .BuildIdentity = "fixture",
            },
        .RegisterModule = RegisterSharedProbe,
    };
    return &kDefinition;
}
} // namespace pg::fixture
