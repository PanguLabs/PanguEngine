#include "ModuleDescriptor.h"
#include "Result.h"
#include <EngineHostModule.h>
#include <ModuleBuiltin.h>
#include <array>

namespace pg::engine
{
namespace
{
Status Register(module::RegistrationContext& /*context*/) noexcept { return {}; }
constexpr std::array kDependencies{
    module::ModuleDependency{.Id = module::kModuleRuntimeModuleId, .Version = {.Major = 1, .Minor = 0, .Patch = 0}},
};
constexpr module::ModuleDefinition kDefinition{
    .Descriptor =
        {
            .Id = kEngineHostModuleId,
            .Version = {.Major = 1, .Minor = 0, .Patch = 0},
            .ProtocolVersion = 1,
            .Dependencies = kDependencies,
            .DisplayName = "EngineHost",
            .BuildIdentity = {},
        },
    .RegisterModule = Register,
};
} // namespace
const module::ModuleDefinition* QueryEngineHostModule() noexcept { return &kDefinition; }
} // namespace pg::engine
