#include "FixtureModules.h"

#include <ModuleDescriptor.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>

#include <expected>
#include <memory>
#include <vector>

namespace pg::module::fixture
{
namespace
{
[[nodiscard]] pg::Status Register(RegistrationContext& context) noexcept
{
    return context.DeclareFactory(kSecondDeclaration, pg::service::Scope::kHost,
                                  ScopeFactory(
                                      [](ScopeBuildContext& build) noexcept -> pg::Status
                                      {
                                          const auto access =
                                              build.Own(std::make_unique<FixtureValue>(std::vector<int>{22}));
                                          return access ? pg::Status{} : std::unexpected(access.error());
                                      }),
                                  {}, "second");
}
constexpr ModuleDefinition kDefinition{
    .Descriptor = {.Id = kSecondId, .Dependencies = {}, .DisplayName = "Second", .BuildIdentity = {}},
    .RegisterModule = Register,
};
} // namespace

const ModuleDefinition* QuerySecond() noexcept { return &kDefinition; }
} // namespace pg::module::fixture
