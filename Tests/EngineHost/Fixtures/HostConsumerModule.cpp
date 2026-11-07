#include "HostConsumerModule.h"
#include "HostServiceModule.h"
#include <HostTestState.h>
#include <ModuleDescriptor.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>
#include <algorithm>
#include <array>
#include <exception>
#include <expected>
#include <memory>
#include <utility>
#include <vector>

namespace pg::engine::test
{
namespace
{
void Record(HostTestState& state, HostTestEvent event, module::ScopeHandle scope = {})
{
    state.Events.push_back({.Event = event, .Scope = scope, .Owner = consumer::kModuleId});
}
struct ConsumerService
{
    std::shared_ptr<HostTestState> State;
    module::ScopeHandle Scope;
};
struct ConsumerDeleter
{
    void operator()(ConsumerService* value) const noexcept
    {
        ++value->State->DestroyCount;
        Record(*value->State, HostTestEvent::kDestroy, value->Scope);
        Record(*value->State, HostTestEvent::kDeleter, value->Scope);
        std::default_delete<ConsumerService>{}(value);
    }
};
struct FactoryLifetime
{
    FactoryLifetime() = default;
    FactoryLifetime(const FactoryLifetime&) = delete;
    FactoryLifetime& operator=(const FactoryLifetime&) = delete;
    FactoryLifetime(FactoryLifetime&&) = delete;
    FactoryLifetime& operator=(FactoryLifetime&&) = delete;
    std::shared_ptr<HostTestState> State;
    ~FactoryLifetime() { Record(*State, HostTestEvent::kFactoryDestroy); }
};
module::ScopeFactory Factory(const std::shared_ptr<HostTestState>& state)
{
    auto lifetime = std::make_unique<FactoryLifetime>();
    lifetime->State = state;
    return module::ScopeFactory(
        [lifetime = std::move(lifetime)](module::ScopeBuildContext& build) noexcept -> pg::Status
        {
            const auto& state = lifetime->State;
            ++state->FactoryCount;
            Record(*state, HostTestEvent::kFactory, build.Scope());
            auto value = std::make_unique<ConsumerService>(ConsumerService{.State = state, .Scope = build.Scope()});
            const auto owned = build.Own(std::unique_ptr<ConsumerService, ConsumerDeleter>(value.release()));
            return owned ? pg::Status{} : pg::Status{std::unexpected(owned.error())};
        });
}
pg::Status Register(module::RegistrationContext& context) noexcept
{
    const auto state = CurrentHostTestState();
    if (!state)
    {
        std::terminate();
    }
    constexpr std::array kHostDependencies{
        module::ScopeDependency{.Id = kHostDeclaration, .Scope = service::Scope::kHost},
    };
    constexpr std::array kWorldDependencies{
        module::ScopeDependency{.Id = kContextDeclaration, .Scope = service::Scope::kContext},
        module::ScopeDependency{.Id = kWorldDeclaration, .Scope = service::Scope::kWorld},
    };
    if (auto result = context.DeclareFactory(consumer::kHostDeclaration, service::Scope::kHost, Factory(state),
                                             kHostDependencies);
        !result)
    {
        return result;
    }
    return context.DeclareFactory(consumer::kWorldDeclaration, service::Scope::kWorld, Factory(state),
                                  kWorldDependencies);
}
constexpr std::array kDependencies{module::ModuleDependency{.Id = kModuleId, .Version = {.Major = 1}}};
constexpr module::ModuleDefinition kDefinition{
    .Descriptor =
        {
            .Id = consumer::kModuleId,
            .Version = {.Major = 1},
            .Dependencies = kDependencies,
            .DisplayName = "HostConsumerFixture",
            .BuildIdentity = "host-consumer-fixture",
        },
    .RegisterModule = Register,
};
#ifdef PG_TEST_MODULAR
struct NativeLifetime
{
    NativeLifetime() = default;
    NativeLifetime(const NativeLifetime&) = delete;
    NativeLifetime& operator=(const NativeLifetime&) = delete;
    NativeLifetime(NativeLifetime&&) = delete;
    NativeLifetime& operator=(NativeLifetime&&) = delete;
    std::vector<std::shared_ptr<HostTestState>> States;
    ~NativeLifetime()
    {
        for (const auto& state : States)
        {
            ++state->NativeUnloadCount;
            Record(*state, HostTestEvent::kNativeUnload);
        }
    }
};
NativeLifetime& NativeObservations()
{
    static NativeLifetime lifetime;
    return lifetime;
}
#endif
} // namespace
const module::ModuleDefinition* QueryHostConsumerFixture() noexcept
{
#ifdef PG_TEST_MODULAR
    if (const auto state = CurrentHostTestState())
    {
        auto& states = NativeObservations().States;
        if (std::ranges::find(states, state) == states.end())
        {
            states.push_back(state);
        }
    }
#endif
    return &kDefinition;
}
} // namespace pg::engine::test
