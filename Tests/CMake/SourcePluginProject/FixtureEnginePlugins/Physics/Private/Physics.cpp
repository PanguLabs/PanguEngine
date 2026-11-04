#include "FixtureModules.h"
#include "ModuleDeclaration.h"
#include "ModuleDescriptor.h"
#include "Result.h"

#include <RegistrationContext.h>
#include <cstddef>

namespace pg::fixture
{
namespace
{
[[nodiscard]] pg::Status RegisterPhysics(module::RegistrationContext& context) noexcept
{
    return context.Declare(module::DeclarationId{.Bytes = {std::byte{0x11}}}, "Physics");
}
} // namespace

const module::ModuleDefinition* QueryPhysics() noexcept
{
    static const module::ModuleDefinition kDefinition{
        .Descriptor = {.Id = kPhysicsId, .Dependencies = {}, .DisplayName = "Physics", .BuildIdentity = "fixture"},
        .RegisterModule = RegisterPhysics,
    };
    return &kDefinition;
}
} // namespace pg::fixture
