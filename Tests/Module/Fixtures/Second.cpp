#include "FixtureModules.h"

#include <ModuleDescriptor.h>
#include <RegistrationContext.h>
#include <Result.h>

namespace pg::module::fixture
{
namespace
{
pg::Status Register(RegistrationContext& context) noexcept { return context.Declare(kSecondDeclaration, "second"); }
constexpr ModuleDefinition kDefinition{
    .Descriptor = {.Id = kSecondId, .Dependencies = {}, .DisplayName = "Second", .BuildIdentity = {}},
    .RegisterModule = Register,
};
} // namespace

const ModuleDefinition* QuerySecond() noexcept { return &kDefinition; }
} // namespace pg::module::fixture
