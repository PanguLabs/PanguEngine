#include "EngineHostTestFixture.h"
#include "HostConsumerModule.h"
#include "HostServiceModule.h"
#include <EngineHost.h>
#include <EngineHostTestAccess.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <HostTestState.h>
#include <ModuleError.h>
#include <ModuleManager.h>
#include <ModuleTestAccess.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#ifdef PG_TEST_MODULAR
#include "HostConsumerFixturePath.h"
#endif

namespace pg::engine::test
{
TEST(EngineHostLifetime, ConsumersDrainBeforeProviders)
{
    const auto state = std::make_shared<HostTestState>();
    SetHostTestState(state);
    auto candidates = std::vector<module::ModuleLoadCandidate>{HostCandidates().front()};
#ifdef PG_TEST_MODULAR
    candidates.emplace_back(
        module::SharedModuleCandidate{.ExpectedId = consumer::kModuleId, .Path = kHostConsumerFixturePath});
#else
    candidates.emplace_back(module::StaticModuleCandidate{QueryHostConsumerFixture});
#endif
    EngineHost host;
    HostOperationReport report;
    HostStartupConfig config;
    config.Logging = pg::engine::test::DisabledLogging();
    config.Modules = candidates;
    ASSERT_TRUE(host.Start(config, report));
    const auto hostScope = host.HostScope();
    const auto context = host.CreateContext(report);
    ASSERT_TRUE(context);
    const auto world = host.CreateWorld(*context, report);
    ASSERT_TRUE(world);
    auto contextAccess = AcquireContextValue(host, *context);
    auto worldAccess = AcquireWorldValue(host, *world);
    ASSERT_TRUE(contextAccess);
    ASSERT_TRUE(worldAccess);
#ifdef PG_TEST_MODULAR
    auto* manager = detail::EngineHostTestAccess::Manager(host);
    const auto consumerHandle = manager->Find(consumer::kModuleId);
    ASSERT_TRUE(consumerHandle.has_value());
    if (!consumerHandle)
    {
        return;
    }
    module::detail::ModuleManagerTestAccess::FailNextNativeClose(*manager, *consumerHandle);
#endif
    ASSERT_TRUE(host.BeginShutdown(report));
#ifdef PG_TEST_MODULAR
    const auto pending = host.TryFinishShutdown(report);
    EXPECT_FALSE(pending);
    if (!pending)
    {
        EXPECT_EQ(pending.error(), module::MakeModuleError(module::ModuleError::kLibraryCloseFailed));
    }
    // 消费方原生释放尚未确认，Provider 必须仍有原生所有者。
    EXPECT_EQ(state->NativeUnloadCount, 0U);
    const auto providerHandle = manager->Find(kModuleId);
    ASSERT_TRUE(providerHandle.has_value());
    if (providerHandle)
    {
        const auto provider = manager->Inspect(*providerHandle);
        ASSERT_TRUE(provider);
        EXPECT_NE(provider->State, module::ModuleState::kUnloaded);
    }
#endif
    ASSERT_TRUE(host.TryFinishShutdown(report));
    EXPECT_EQ(host.GetState(), HostState::kClosed);
    const auto first = [&](module::ModuleId owner, HostTestEvent event, module::ScopeHandle scope = {})
    {
        const auto found = std::ranges::find_if(
            state->Events, [&](const auto& entry)
            { return entry.Owner == owner && entry.Event == event && (!scope.IsValid() || entry.Scope == scope); });
        EXPECT_NE(found, state->Events.end());
        return static_cast<std::size_t>(found - state->Events.begin());
    };
    const auto last = [&](module::ModuleId owner, HostTestEvent event)
    {
        std::size_t result{};
        bool found{};
        for (std::size_t index = 0; index < state->Events.size(); ++index)
        {
            if (state->Events.at(index).Owner == owner && state->Events.at(index).Event == event)
            {
                result = index;
                found = true;
            }
        }
        EXPECT_TRUE(found);
        return result;
    };
    EXPECT_LT(first(consumer::kModuleId, HostTestEvent::kDeleter, *world),
              first(kModuleId, HostTestEvent::kDestroy, *world));
    EXPECT_LT(first(consumer::kModuleId, HostTestEvent::kDeleter, *world),
              first(kModuleId, HostTestEvent::kDestroy, *context));
    EXPECT_LT(first(consumer::kModuleId, HostTestEvent::kDeleter, hostScope),
              first(kModuleId, HostTestEvent::kDestroy, hostScope));
    for (const auto owner : std::array{kModuleId, consumer::kModuleId})
    {
        EXPECT_LT(last(owner, HostTestEvent::kDestroy), first(owner, HostTestEvent::kFactoryDestroy));
        EXPECT_LT(last(owner, HostTestEvent::kDeleter), first(owner, HostTestEvent::kFactoryDestroy));
#ifdef PG_TEST_MODULAR
        EXPECT_LT(last(owner, HostTestEvent::kFactoryDestroy), first(owner, HostTestEvent::kNativeUnload));
#endif
    }
#ifdef PG_TEST_MODULAR
    EXPECT_EQ(state->NativeUnloadCount, 2U);
    EXPECT_LT(first(consumer::kModuleId, HostTestEvent::kNativeUnload), first(kModuleId, HostTestEvent::kNativeUnload));
#endif
    auto contextCopy = *contextAccess;
    auto worldCopy = *worldAccess;
    const auto contextDenied = contextCopy.TryBorrow();
    const auto worldDenied = worldCopy.TryBorrow();
    ASSERT_FALSE(contextDenied);
    ASSERT_FALSE(worldDenied);
    EXPECT_EQ(contextDenied.error(), module::MakeModuleError(module::ModuleError::kNotReady));
    EXPECT_EQ(worldDenied.error(), module::MakeModuleError(module::ModuleError::kNotReady));
    *contextAccess = {};
    *worldAccess = {};
    contextCopy = {};
    worldCopy = {};
}
} // namespace pg::engine::test
