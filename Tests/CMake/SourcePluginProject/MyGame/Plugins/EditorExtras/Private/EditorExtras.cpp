#include "FixtureModules.h"
#include "ModuleDeclaration.h"
#include "ModuleDescriptor.h"
#include "Result.h"

#include <RegistrationContext.h>

#include <array>
#include <cstddef>

namespace pg::fixture
{
namespace
{
[[nodiscard]] pg::Status RegisterEditorExtras(module::RegistrationContext& context) noexcept
{
    return context.Declare(module::DeclarationId{.Bytes = {std::byte{0x13}}}, "EditorExtras");
}
} // namespace

const module::ModuleDefinition* QueryEditorExtras() noexcept
{
    static constexpr std::array kDependencies{module::ModuleDependency{.Id = kPhysicsId}};
    static const module::ModuleDefinition kDefinition{
        .Descriptor =
            {
                .Id = kEditorExtrasId,
                .Dependencies = kDependencies,
                .DisplayName = "EditorExtras",
                .BuildIdentity = "fixture",
            },
        .RegisterModule = RegisterEditorExtras,
    };
    return &kDefinition;
}
} // namespace pg::fixture
