#include "Identity.h"
#include <ModuleDescriptor.h>
#include <RegistrationContext.h>
#include <Result.h>
#if PG_META_HAS_DESCRIPTION
#include <ModuleMeta.gen.h> // IWYU pragma: keep
#endif
namespace pg::test
{
namespace
{
Status Register(module::RegistrationContext& /*unused*/) noexcept { return {}; }
constinit const module::ModuleDefinition kDefinition{
    .Descriptor = {.Id = kActorModule, .Dependencies = {}, .DisplayName = "Actor", .BuildIdentity = {}},
    .RegisterModule = Register,
#if PG_META_HAS_DESCRIPTION
    .Metadata = &PG_TEST_PROVIDER,
#endif
};
} // namespace
const module::ModuleDefinition* QueryActor() noexcept { return &kDefinition; }
} // namespace pg::test
