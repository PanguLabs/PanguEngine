#include "MinimalService.h"
#include <EngineHostModule.h>
#include <ModuleDescriptor.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>
#include <array>
#include <expected>
#include <memory>

namespace pg::sample
{
namespace
{
[[nodiscard]] Status Register(module::RegistrationContext& context) noexcept
{
    return context.DeclareFactory(kContextDeclaration, service::Scope::kContext,
                                  module::ScopeFactory(
                                      [](module::ScopeBuildContext& build) noexcept -> Status
                                      {
                                          const auto value =
                                              build.Own(std::make_unique<CounterService>(CounterService{7}));
                                          return value ? Status{} : Status{std::unexpected(value.error())};
                                      }),
                                  {}, "CounterService");
}
constexpr std::array kDependencies{
    module::ModuleDependency{.Id = engine::kEngineHostModuleId, .Version = {.Major = 1}},
};
constexpr module::ModuleDefinition kDefinition{
    .Descriptor =
        {
            .Id = kModuleId,
            .Version = {.Major = 1},
            .Dependencies = kDependencies,
            .DisplayName = "MinimalServices",
            .BuildIdentity = "minimal-host",
        },
    .RegisterModule = Register,
};
} // namespace
const module::ModuleDefinition* QueryMinimalServices() noexcept { return &kDefinition; }
} // namespace pg::sample
