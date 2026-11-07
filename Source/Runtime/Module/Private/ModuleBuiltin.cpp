#include <FoundationModule.h>
#include <ModuleBuiltin.h>
#include <ModuleDescriptor.h>
#include <Result.h>
#include <array>
#include <span>
namespace pg::module
{
namespace
{
Status Register(RegistrationContext& /*context*/) noexcept { return {}; }
constexpr auto kDependencies =
    std::array{ModuleDependency{.Id = kFoundationModuleId, .Version = {.Major = 1, .Minor = 0, .Patch = 0}}};
constexpr ModuleDefinition kDefinition{
    .Descriptor =
        {
            .Id = kModuleRuntimeModuleId,
            .Version = {.Major = 1, .Minor = 0, .Patch = 0},
            .ProtocolVersion = 1,
            .Dependencies = kDependencies,
            .DisplayName = "ModuleRuntime",
            .BuildIdentity = {},
        },
    .RegisterModule = Register,
};
} // namespace
const ModuleDefinition* QueryModuleRuntime() noexcept { return &kDefinition; }
std::span<const QueryModuleFn> BuiltinResidentQueries() noexcept
{
    static const std::array kQueries{QueryFoundationModule, QueryModuleRuntime};
    return kQueries;
}
} // namespace pg::module
