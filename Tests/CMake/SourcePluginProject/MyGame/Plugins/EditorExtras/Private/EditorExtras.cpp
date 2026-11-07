#include "EditorExtrasModule.h"
#include "EditorService.h"
#include "FixtureModules.h"
#include "ModuleDeclaration.h"
#include "ModuleDescriptor.h"
#include "Result.h"

#include <RegistrationContext.h>
#include <ScopeBuildContext.h>
#include <memory>

#include <ScopeFactory.h>
#include <ScopeIdentity.h>
#include <array>
#include <expected>

namespace pg::fixture
{
namespace
{
[[nodiscard]] pg::Status RegisterEditorExtras(module::RegistrationContext& context) noexcept
{
    return context.DeclareFactory(kEditorDeclaration, service::Scope::kContext,
                                  module::ScopeFactory(
                                      [](module::ScopeBuildContext& build) noexcept -> Status
                                      {
                                          const auto value = build.Own(std::make_unique<EditorSession>());
                                          return value ? Status{} : Status{std::unexpected(value.error())};
                                      }),
                                  {}, "EditorExtras");
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
