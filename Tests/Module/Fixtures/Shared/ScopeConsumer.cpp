#include "ScopeTestSupport.h"
#include "SharedFixtureContract.h"

#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>

#include <array>
#include <expected>
#include <memory>
#include <string>
#include <utility>

namespace pg::module::fixture
{
namespace
{
using pg::test::Event;
using pg::test::Observer;
struct ConsumerState
{
    pg::test::ProbeState* Provider;
    explicit ConsumerState(pg::test::ProbeState* provider)
        : Provider(provider)
    {
    }
    ~ConsumerState()
    {
        if (Provider->SceneName.starts_with("World") && Provider->Values.back() == 42)
        {
            Observer().Record(Event::kDependencyUsed);
        }
        Observer().Record(Event::kConsumerDestroy);
    }
    ConsumerState(const ConsumerState&) = delete;
    ConsumerState& operator=(const ConsumerState&) = delete;
    ConsumerState(ConsumerState&&) = delete;
    ConsumerState& operator=(ConsumerState&&) = delete;
};
struct NativeLifetime final
{
    NativeLifetime() noexcept { Observer().Record(Event::kNativeLoad); }
    ~NativeLifetime() { Observer().Record(Event::kNativeUnload); }
    NativeLifetime(const NativeLifetime&) = delete;
    NativeLifetime& operator=(const NativeLifetime&) = delete;
    NativeLifetime(NativeLifetime&&) = delete;
    NativeLifetime& operator=(NativeLifetime&&) = delete;
};
const NativeLifetime kLifetime;
struct FactoryLifetime final
{
    FactoryLifetime() = default;
    FactoryLifetime(const FactoryLifetime&) = delete;
    FactoryLifetime& operator=(const FactoryLifetime&) = delete;
    FactoryLifetime(FactoryLifetime&&) = delete;
    FactoryLifetime& operator=(FactoryLifetime&&) = delete;
    std::string Name{"consumer"};
    ~FactoryLifetime() { Observer().Record(Event::kFactoryDestroy); }
};
struct ConsumerDeleter final
{
    std::string AllocationOwner;
    void operator()(ConsumerState* object) const noexcept
    {
        std::unique_ptr<ConsumerState> owned(object);
        owned.reset();
        Observer().Record(Event::kDeleterDestroy);
    }
};
pg::Status Start(ConsumerState& /*state*/) noexcept
{
    Observer().Record(Event::kConsumerStart);
    return {};
}
void Stop(ConsumerState& /*state*/) noexcept { Observer().Record(Event::kConsumerStop); }
pg::Status Poll(ConsumerState& /*state*/) noexcept
{
    Observer().Record(Event::kConsumerPoll);
    return {};
}
pg::Status Register(RegistrationContext& context) noexcept
{
    Observer().Record(Event::kRegister);
    const std::array dependencies{ScopeDependency{.Id = kScopeWorldDeclaration, .Scope = pg::service::Scope::kWorld}};
    return context.DeclareFactory(
        kScopeConsumerDeclaration, pg::service::Scope::kWorld,
        ScopeFactory(
            [lifetime = std::make_unique<FactoryLifetime>()](ScopeBuildContext& build) noexcept -> pg::Status
            {
                if (Observer().SharedOptions().WrongScopeDependencyType)
                {
                    const auto wrong = build.Dependency<int>(kScopeWorldDeclaration);
                    return wrong ? pg::Status{} : pg::Status{std::unexpected(wrong.error())};
                }
                const auto provider = build.Dependency<pg::test::ProbeState>(kScopeWorldDeclaration);
                if (!provider)
                {
                    return std::unexpected(provider.error());
                }
                if (lifetime->Name.empty() || (*provider)->Values.back() != 42)
                {
                    return std::unexpected(MakeModuleError(ModuleError::kInvalidState));
                }
                auto object = std::unique_ptr<ConsumerState, ConsumerDeleter>(
                    std::make_unique<ConsumerState>(*provider).release(), ConsumerDeleter{lifetime->Name});
                const auto access =
                    build.Own(std::move(object),
                              ScopeHooks<ConsumerState>{.Start = Start, .RequestStop = Stop, .PollStopped = Poll});
                return access ? pg::Status{} : pg::Status{std::unexpected(access.error())};
            }),
        dependencies);
}
constexpr std::array kDependencies{ModuleDependency{.Id = kScopeProviderId, .Version = {.Major = 1}}};
constexpr ModuleDefinition kDefinition{
    .Descriptor =
        {
            .Id = kScopeConsumerId,
            .Version = {.Major = 1},
            .Dependencies = kDependencies,
            .DisplayName = "ScopeConsumer",
            .BuildIdentity = "scope-fixture",
        },
    .RegisterModule = Register,
};
} // namespace
const ModuleDefinition* QueryScopeConsumer() noexcept { return &kDefinition; }
} // namespace pg::module::fixture
