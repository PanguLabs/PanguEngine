#include <FoundationModule.h>
#include <ModuleDescriptor.h>
#include <Result.h>
#include <array>
namespace pg::module
{
namespace
{
Status Register(RegistrationContext& /*context*/) noexcept { return {}; }
constexpr auto kDependencies = std::array<ModuleDependency, 0>{};
constexpr ModuleDefinition kDefinition{
    .Descriptor =
        {
            .Id = kFoundationModuleId,
            .Version = {.Major = 1, .Minor = 0, .Patch = 0},
            .ProtocolVersion = 1,
            .Dependencies = kDependencies,
            .DisplayName = "Foundation",
            .BuildIdentity = {},
        },
    .RegisterModule = Register,
};
} // namespace
const ModuleDefinition* QueryFoundationModule() noexcept { return &kDefinition; }
} // namespace pg::module
