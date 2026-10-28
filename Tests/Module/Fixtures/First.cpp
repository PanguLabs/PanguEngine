#include "FixtureModules.h"

#include <ModuleDescriptor.h>
#include <RegistrationContext.h>
#include <Result.h>

#include <array>

namespace pg::module::fixture
{
namespace
{
pg::Status Register(RegistrationContext& context) noexcept { return context.Declare(kFirstDeclaration, "first"); }
constexpr std::array kDependencies{ModuleDependency{.Id = kSecondId}};
constexpr ModuleDefinition kDefinition{
    .Descriptor = {.Id = kFirstId, .Dependencies = kDependencies, .DisplayName = "First", .BuildIdentity = {}},
    .RegisterModule = Register,
};
} // namespace

const ModuleDefinition* QueryFirst() noexcept { return &kDefinition; }
} // namespace pg::module::fixture
