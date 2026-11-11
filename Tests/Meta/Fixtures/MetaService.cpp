#include "MetaService.h"
#include "MetaModule.h"
#include "ModuleDescriptor.h"
#include "ScopeIdentity.h"
#include <EngineHostModule.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <array>
#include <expected>
#include <memory>
#if PG_META_HAS_DESCRIPTION
#include <ModuleMeta.gen.h>
#endif

namespace pg::test::meta_fixture
{
namespace
{
Status Register(module::RegistrationContext& context) noexcept
{
    return context.DeclareFactory(kActorDeclaration, service::Scope::kContext,
                                  module::ScopeFactory(
                                      [](module::ScopeBuildContext& build) noexcept -> Status
                                      {
                                          const auto value = build.Own(std::make_unique<ActorService>());
                                          return value ? Status{} : Status{std::unexpected(value.error())};
                                      }),
                                  {}, "ActorService");
}
constexpr std::array kDependencies{
    module::ModuleDependency{.Id = engine::kEngineHostModuleId, .Version = {.Major = 1}},
    module::ModuleDependency{.Id = kModuleId, .Version = {.Major = 1}},
};
constexpr module::ModuleDefinition kDefinition{
    .Descriptor =
        {
            .Id = kServiceModuleId,
            .Version = {.Major = 1},
            .Dependencies = kDependencies,
            .DisplayName = "MetaServiceFixture",
            .BuildIdentity = "generated-service",
        },
    .RegisterModule = Register,
#if PG_META_HAS_DESCRIPTION
    .Metadata = &meta::generated::T_PanguMetaServiceFixture::kModuleMetadata,
#endif
};
} // namespace
const module::ModuleDefinition* QueryMetaService() noexcept { return &kDefinition; }
} // namespace pg::test::meta_fixture
