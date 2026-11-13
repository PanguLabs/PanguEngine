#include "SharedFixtureContract.h"

#include <FailureContext.h>
#include <ModuleDeclaration.h>
#include <ModuleDescriptor.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>
#include <ScopeTestSupport.h>
#include <cstddef>
#include <expected>

#include <memory>

namespace pg::module::fixture
{
namespace
{
void Stop([[maybe_unused]] int& object) noexcept {}
Status Poll([[maybe_unused]] int& object, FailureContext& failure) noexcept
{
    const auto error = pg::test::Observer().SharedOptions().DiagnosticPollError;
    if (error)
    {
        failure.SetPrimary(*error, "diagnostic poll failed");
        return std::unexpected(*error);
    }
    return {};
}
Status Register(RegistrationContext& context) noexcept
{
    constexpr DeclarationId kObject{.Bytes = {std::byte{0xa3}}};
    return context.DeclareFactory(kObject, pg::service::Scope::kHost,
                                  ScopeFactory(
                                      [](ScopeBuildContext& build) noexcept -> Status
                                      {
                                          const auto object =
                                              build.Own(std::make_unique<int>(1),
                                                        ScopeHooks<int>{.RequestStop = Stop, .PollStopped = Poll});
                                          return object ? Status{} : Status{std::unexpected(object.error())};
                                      }));
}
} // namespace
const ModuleDefinition* QueryDiagnosticsCleanup() noexcept
{
    static const ModuleDefinition kDefinition{
        .Descriptor = {.Id = kDiagnosticsCleanupId, .Dependencies = {}, .DisplayName = {}, .BuildIdentity = {}},
        .RegisterModule = Register};
    return &kDefinition;
}
} // namespace pg::module::fixture
