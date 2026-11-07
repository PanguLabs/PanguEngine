#include "ModuleError.h"
#include "ScopeAccess.h"
#include "ScopeBuildContext.h"
#include "ScopeIdentity.h"
#include "SharedFixtureContract.h"

#include <ModuleDescriptor.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeFactory.h>
#include <ScopeTestSupport.h>

#include <array>
#include <expected>
#include <memory>
#include <utility>

namespace pg::module::fixture
{
namespace
{
struct NativeLifetime final
{
    NativeLifetime() noexcept { pg::test::Observer().Record(pg::test::Event::kNativeLoad); }
    ~NativeLifetime() { pg::test::Observer().Record(pg::test::Event::kNativeUnload); }
    NativeLifetime(const NativeLifetime&) = delete;
    NativeLifetime& operator=(const NativeLifetime&) = delete;
    NativeLifetime(NativeLifetime&&) = delete;
    NativeLifetime& operator=(NativeLifetime&&) = delete;
};
const NativeLifetime kLifetime;
struct ConsumerState
{
    ScopeBorrow<pg::test::ProbeState> Provider;
    unsigned BusyPolls{};
    ConsumerState(ScopeBorrow<pg::test::ProbeState> provider, unsigned polls)
        : Provider(std::move(provider))
        , BusyPolls(polls)
    {
    }
    ~ConsumerState()
    {
        if (Provider->Values.front() == SharedProviderValue())
        {
            pg::test::Observer().Record(pg::test::Event::kDependencyUsed);
        }
        pg::test::Observer().Record(pg::test::Event::kConsumerDestroy);
    }
    ConsumerState(const ConsumerState&) = delete;
    ConsumerState& operator=(const ConsumerState&) = delete;
    ConsumerState(ConsumerState&&) = delete;
    ConsumerState& operator=(ConsumerState&&) = delete;
};
pg::Status Start(ConsumerState& /*state*/) noexcept
{
    auto& observer = pg::test::Observer();
    observer.Record(pg::test::Event::kConsumerStart);
    if (observer.SharedOptions().FailSecondConsumerStart && observer.Count(pg::test::Event::kConsumerStart) == 2)
    {
        return std::unexpected(MakeModuleError(ModuleError::kInvalidState));
    }
    return {};
}
void Stop(ConsumerState& /*state*/) noexcept { pg::test::Observer().Record(pg::test::Event::kConsumerStop); }
pg::Status Poll(ConsumerState& state) noexcept
{
    pg::test::Observer().Record(pg::test::Event::kConsumerPoll);
    if (state.BusyPolls != 0)
    {
        --state.BusyPolls;
        return std::unexpected(MakeModuleError(ModuleError::kBusy));
    }
    return {};
}
pg::Status Register(RegistrationContext& context) noexcept
{
    pg::test::Observer().Record(pg::test::Event::kRegister);
    if (pg::test::Observer().SharedOptions().Services)
    {
        const std::array dependencies{ScopeDependency{.Id = kSharedProviderDeclaration}};
        return context.DeclareFactory(
            kSharedConsumerDeclaration, pg::service::Scope::kContext,
            ScopeFactory(
                [](ScopeBuildContext& build) noexcept -> pg::Status
                {
                    const auto dependency = build.Dependency<pg::test::ProbeState>(kSharedProviderDeclaration);
                    if (!dependency)
                    {
                        return std::unexpected(dependency.error());
                    }
                    auto borrow = pg::test::Observer().Access().TryBorrow();
                    if (!borrow)
                    {
                        return std::unexpected(borrow.error());
                    }
                    if (borrow->Get() != *dependency)
                    {
                        return std::unexpected(MakeModuleError(ModuleError::kTypeMismatch));
                    }
                    auto access =
                        build.Own(std::make_unique<ConsumerState>(
                                      std::move(*borrow), pg::test::Observer().SharedOptions().ConsumerBusyPolls),
                                  ScopeHooks<ConsumerState>{.Start = Start, .RequestStop = Stop, .PollStopped = Poll});
                    return access ? pg::Status{} : pg::Status{std::unexpected(access.error())};
                }),
            dependencies);
    }
    return context.Declare(kSharedConsumerDeclaration, "shared-consumer");
}
constexpr std::array kDependencies{ModuleDependency{.Id = kSharedProviderId, .Version = {.Major = 1}}};
constexpr ModuleDefinition kDefinition{
    .Descriptor =
        {
            .Id = kSharedConsumerId,
            .Version = {.Major = 1, .Minor = 0, .Patch = 0},
            .Dependencies = kDependencies,
            .DisplayName = "SharedConsumer",
            .BuildIdentity = "shared-fixture",
        },
    .RegisterModule = Register,
};
} // namespace
const ModuleDefinition* QuerySharedConsumer() noexcept { return SharedProviderValue() == 42 ? &kDefinition : nullptr; }
} // namespace pg::module::fixture
