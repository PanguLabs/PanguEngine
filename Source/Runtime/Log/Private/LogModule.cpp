#include <LogModule.h>

#include <FoundationModule.h>
#include <ModuleDescriptor.h>
#include <Result.h>

#include <array>

namespace pg::log
{
namespace
{
Status Register(module::RegistrationContext& /*context*/) noexcept { return {}; }
constexpr auto kDependencies = std::array{
    module::ModuleDependency{.Id = module::kFoundationModuleId, .Version = {.Major = 1, .Minor = 0, .Patch = 0}},
};
constexpr module::ModuleDefinition kDefinition{
    .Descriptor =
        {
            .Id = kLogModuleId,
            .Version = {.Major = 1, .Minor = 0, .Patch = 0},
            .ProtocolVersion = 1,
            .Dependencies = kDependencies,
            .DisplayName = "Log",
            .BuildIdentity = {},
        },
    .RegisterModule = Register,
};
} // namespace
const module::ModuleDefinition* QueryLogModule() noexcept { return &kDefinition; }
} // namespace pg::log
