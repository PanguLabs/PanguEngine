#include "HostServiceModule.h"
#include "FailureContext.h"
#include <EngineHostModule.h>
#include <HostTestState.h>
#include <ModuleDescriptor.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <expected>
#include <memory>
#include <utility>
#include <vector>

namespace pg::engine::test
{
namespace
{
// 观测仅属于夹具；公开服务值类型不依赖测试支持或 Manager 基类。
struct HostService : HostServiceValue
{
    std::shared_ptr<HostTestState> State;
    module::ScopeHandle Scope;
};
HostService& Observation(HostServiceValue& value) noexcept
{
    // 所有 hook/deleter 输入均由下方 Factory 创建为 HostService，公开 RTTI 仍为普通值类型。
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    return static_cast<HostService&>(value);
}
void Record(HostTestState& state, HostTestEvent event, module::ScopeHandle scope = {})
{
    state.Events.push_back({.Event = event, .Scope = scope, .Owner = kModuleId});
}
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
            if (state->OnNativeUnload)
            {
                state->OnNativeUnload();
            }
        }
    }
};
NativeLifetime& NativeObservations()
{
    static NativeLifetime lifetime;
    return lifetime;
}
#endif

// ScopeHooks 固定以可变引用接收服务。
// NOLINTNEXTLINE(misc-const-correctness)
pg::Status Start(HostServiceValue& value, pg::FailureContext& /*failure*/) noexcept
{
    const auto& service = Observation(value);
    Record(*service.State, HostTestEvent::kStart, service.Scope);
    return service.State->FailStart ? pg::Status{std::unexpected(*service.State->FailStart)} : pg::Status{};
}
// ScopeHooks 固定以可变引用接收服务。
// NOLINTNEXTLINE(misc-const-correctness)
void Stop(HostServiceValue& value) noexcept
{
    const auto& service = Observation(value);
    Record(*service.State, HostTestEvent::kStop, service.Scope);
    if (service.State->OnStop)
    {
        service.State->OnStop();
    }
}
// ScopeHooks 固定以可变引用接收服务。
// NOLINTNEXTLINE(misc-const-correctness)
pg::Status Poll(HostServiceValue& value, pg::FailureContext& /*failure*/) noexcept
{
    const auto& service = Observation(value);
    ++service.State->PollCount;
    return service.State->PollError ? pg::Status{std::unexpected(*service.State->PollError)} : pg::Status{};
}
struct ServiceDeleter
{
    void operator()(HostServiceValue* value) const noexcept
    {
        auto* service = &Observation(*value);
        ++service->State->DestroyCount;
        Record(*service->State, HostTestEvent::kDestroy, service->Scope);
        Record(*service->State, HostTestEvent::kDeleter, service->Scope);
        if (service->State->OnDestroy)
        {
            service->State->OnDestroy();
        }
        std::default_delete<HostService>{}(service);
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
module::ScopeFactory Factory(const std::shared_ptr<HostTestState>& state, int value)
{
    auto lifetime = std::make_unique<FactoryLifetime>();
    lifetime->State = state;
    return module::ScopeFactory(
        [value, lifetime = std::move(lifetime)](module::ScopeBuildContext& build) noexcept -> pg::Status
        {
            const auto& state = lifetime->State;
            ++state->FactoryCount;
            Record(*state, HostTestEvent::kFactory, build.Scope());
            if (state->OnFactory)
            {
                state->OnFactory();
            }
            if (state->FailFactory)
            {
                return std::unexpected(*state->FailFactory);
            }
            auto service = std::make_unique<HostService>(HostService{{value}, state, build.Scope()});
            const auto result = build.Own(
                std::unique_ptr<HostServiceValue, ServiceDeleter>(service.release()),
                module::ScopeHooks<HostServiceValue>{.Start = Start, .RequestStop = Stop, .PollStopped = Poll});
            return result ? pg::Status{} : pg::Status{std::unexpected(result.error())};
        });
}
pg::Status Register(module::RegistrationContext& context) noexcept
{
    const auto state = CurrentHostTestState();
    if (!state)
    {
        std::terminate();
    }
    ++state->RegisterCount;
    if (state->FailRegister)
    {
        return std::unexpected(*state->FailRegister);
    }
    constexpr std::array kDeclarations{
        kHostDeclaration,
        kContextDeclaration,
        kWorldDeclaration,
        kSecondContextDeclaration,
    };
    constexpr std::array kKinds{
        service::Scope::kHost,
        service::Scope::kContext,
        service::Scope::kWorld,
        service::Scope::kContext,
    };
    for (std::size_t index = 0; index < kDeclarations.size(); ++index)
    {
        if (auto status = context.DeclareFactory(kDeclarations.at(index), kKinds.at(index),
                                                 Factory(state, static_cast<int>(index + 1)));
            !status)
        {
            return status;
        }
    }
    return context.Declare(kMetadataDeclaration, "仅元数据");
}
constexpr std::array kDependencies{module::ModuleDependency{.Id = kEngineHostModuleId, .Version = {.Major = 1}}};
constexpr module::ModuleDefinition kDefinition{
    .Descriptor =
        {
            .Id = kModuleId,
            .Version = {.Major = 1},
            .Dependencies = kDependencies,
            .DisplayName = "HostServiceFixture",
            .BuildIdentity = "host-service-fixture",
        },
    .RegisterModule = Register,
};
} // namespace
const module::ModuleDefinition* QueryHostServiceFixture() noexcept
{
    if (const auto state = CurrentHostTestState())
    {
        ++state->QueryCount;
#ifdef PG_TEST_MODULAR
        auto& states = NativeObservations().States;
        if (std::ranges::find(states, state) == states.end())
        {
            states.push_back(state);
        }
#endif
        if (state->OnQuery)
        {
            state->OnQuery();
        }
    }
    return &kDefinition;
}
} // namespace pg::engine::test
