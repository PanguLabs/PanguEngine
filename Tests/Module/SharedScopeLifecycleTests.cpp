#include "SharedFixtureContract.h"
#include "SharedFixturePaths.h"
#include "Support/ScopeTestSupport.h"

#include <ModuleBuiltin.h>
#include <ModuleError.h>
#include <ModuleManager.h>
#include <ModuleOperation.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeIdentity.h>
#include <gtest/gtest.h>
#include <iterator>
#include <ranges>

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <vector>

namespace
{
using namespace pg::module;
using namespace pg::module::fixture;
using pg::service::Scope;
using pg::test::Event;

class SharedScopeLifecycle : public testing::Test
{
protected:
    void SetUp() override { pg::test::Observer().Reset(); }
    static pg::Result<ModuleLoadResult> Load(ModuleManager& manager, ModuleOperationReport& report,
                                             bool consumer = false)
    {
        const std::array candidates{
            ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kScopeProviderId, .Path = kScopeProviderPath}},
            ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kScopeConsumerId, .Path = kScopeConsumerPath}},
        };
        return manager.Load(std::span(candidates).first(consumer ? 2U : 1U), {}, report);
    }
};

TEST_F(SharedScopeLifecycle, ExistingOwners)
{
    auto const& observer = pg::test::Observer();
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    const auto contextA = manager->CreateScope(Scope::kContext, manager->HostScope(), report);
    ASSERT_TRUE(contextA);
    const auto worldA = manager->CreateScope(Scope::kWorld, *contextA, report);
    ASSERT_TRUE(worldA);
    const auto loaded = Load(*manager, report);
    ASSERT_TRUE(loaded);
    const auto provider = loaded->Modules.front();
    ASSERT_TRUE(manager->Register(provider, report));
    EXPECT_FALSE(observer.Access(*contextA).TryBorrow());
    ASSERT_TRUE(manager->Activate(provider, report));
    const auto contextB = manager->CreateScope(Scope::kContext, manager->HostScope(), report);
    ASSERT_TRUE(contextB);
    const auto worldB = manager->CreateScope(Scope::kWorld, *contextB, report);
    ASSERT_TRUE(worldB);
    {
        const auto contextABorrow = observer.Access(*contextA).TryBorrow();
        const auto contextBBorrow = observer.Access(*contextB).TryBorrow();
        const auto worldABorrow = observer.Access(*worldA).TryBorrow();
        const auto worldBBorrow = observer.Access(*worldB).TryBorrow();
        ASSERT_TRUE(contextABorrow);
        ASSERT_TRUE(contextBBorrow);
        ASSERT_TRUE(worldABorrow);
        ASSERT_TRUE(worldBBorrow);
        const auto* contextAObject = contextABorrow->Get();
        const auto* contextBObject = contextBBorrow->Get();
        EXPECT_NE(contextAObject, contextBObject);
        EXPECT_NE(worldABorrow->Get(), worldBBorrow->Get());
        const auto originalScopeValue = (*worldABorrow)->SceneName;
        EXPECT_EQ(originalScopeValue, "World A");
        (*worldBBorrow)->Values.push_back(99);
        EXPECT_EQ((*worldABorrow)->Values, (std::vector<int>{1, 42}));
        EXPECT_EQ((*contextABorrow)->Starts, 1U);
    }
    EXPECT_EQ(observer.RegisterCount(), 1U);
    ASSERT_TRUE(manager->BeginUnload(provider, report));
    ASSERT_TRUE(manager->TryFinishUnload(provider, report));
    EXPECT_FALSE(observer.Access(*contextA).TryBorrow());
    EXPECT_FALSE(observer.Access(*worldB).TryBorrow());
    EXPECT_EQ(observer.Count(Event::kStart), 4U);
    EXPECT_EQ(observer.Count(Event::kStop), 4U);
    EXPECT_EQ(observer.Count(Event::kPoll), 4U);
    EXPECT_EQ(observer.Count(Event::kDestroy), 4U);
    EXPECT_EQ(observer.Count(Event::kDeleterDestroy), 4U);
    EXPECT_EQ(observer.Count(Event::kProviderDestroy), 4U);
    EXPECT_EQ(observer.Count(Event::kPrivateDestroy), 1U);
    EXPECT_EQ(observer.Count(Event::kFactoryDestroy), 3U);
    EXPECT_EQ(observer.NativeUnloadCount(), 1U);
    const auto events = observer.Events();
    const auto firstStop = std::ranges::find(events, Event::kStop);
    const auto firstPoll = std::ranges::find(events, Event::kPoll);
    const auto firstDestroy = std::ranges::find(events, Event::kDestroy);
    const auto firstFactory = std::ranges::find(events, Event::kFactoryDestroy);
    const auto native = std::ranges::find(events, Event::kNativeUnload);
    EXPECT_LT(firstStop, firstPoll);
    EXPECT_LT(firstPoll, firstDestroy);
    EXPECT_LT(firstDestroy, firstFactory);
    EXPECT_LT(firstFactory, native);
    const auto lastObject =
        std::ranges::find_if(events | std::views::reverse,
                             [](Event event)
                             {
                                 return event == Event::kDestroy || event == Event::kProviderDestroy ||
                                        event == Event::kDeleterDestroy || event == Event::kPrivateDestroy;
                             });
    const auto lastFactory = std::ranges::find(events | std::views::reverse, Event::kFactoryDestroy);
    ASSERT_NE(lastObject, events.rend());
    ASSERT_NE(lastFactory, events.rend());
    EXPECT_LT(std::prev(lastObject.base()), firstFactory);
    EXPECT_LT(std::prev(lastFactory.base()), native);
}

TEST_F(SharedScopeLifecycle, LocalRollback)
{
    for (const bool failStart : {false, true})
    {
        auto& observer = pg::test::Observer();
        observer.Reset();
        observer.ConfigureSharedFixture(
            {.FailContextFactoryAt = failStart ? 0U : 2U, .FailContextStartAt = failStart ? 2U : 0U});
        RegistrationScope declarations;
        ModuleOperationReport report;
        auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
        ASSERT_TRUE(manager);
        const auto loaded = Load(*manager, report);
        ASSERT_TRUE(loaded);
        const auto provider = loaded->Modules.front();
        ASSERT_TRUE(manager->Register(provider, report));
        ASSERT_TRUE(manager->Activate(provider, report));
        const auto a = manager->CreateScope(Scope::kContext, manager->HostScope(), report);
        ASSERT_TRUE(a);
        const auto b = manager->CreateScope(Scope::kContext, manager->HostScope(), report);
        const auto rejectedScope = report.Scope;
        ASSERT_FALSE(b);
        EXPECT_EQ(b.error(), MakeModuleError(ModuleError::kInvalidState));
        EXPECT_FALSE(observer.Access(rejectedScope).TryBorrow());
        EXPECT_EQ(observer.Count(Event::kDestroy), 1U);
        ASSERT_TRUE(observer.Access(*a).TryBorrow());
        const auto c = manager->CreateScope(Scope::kContext, manager->HostScope(), report);
        ASSERT_TRUE(c);
        EXPECT_EQ(c->Slot, rejectedScope.Slot);
        EXPECT_NE(c->Generation, rejectedScope.Generation);
        EXPECT_TRUE(observer.Access(*c).TryBorrow());
        EXPECT_TRUE(observer.Access(*a).TryBorrow());
        EXPECT_EQ(manager->Inspect(provider)->State, ModuleState::kActive);
        EXPECT_EQ(observer.RegisterCount(), 1U);
        ASSERT_TRUE(manager->BeginUnload(provider, report));
        ASSERT_TRUE(manager->TryFinishUnload(provider, report));
        EXPECT_EQ(observer.Count(Event::kDestroy), 3U);
        EXPECT_EQ(observer.NativeUnloadCount(), 1U);
    }
}

TEST_F(SharedScopeLifecycle, ActivationRollback)
{
    auto& observer = pg::test::Observer();
    observer.ConfigureSharedFixture({.FailContextStartAt = 2});
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    const auto a = manager->CreateScope(Scope::kContext, manager->HostScope(), report);
    const auto b = manager->CreateScope(Scope::kContext, manager->HostScope(), report);
    const auto c = manager->CreateScope(Scope::kContext, manager->HostScope(), report);
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    ASSERT_TRUE(c);
    const auto loaded = Load(*manager, report);
    ASSERT_TRUE(loaded);
    const auto provider = loaded->Modules.front();
    ASSERT_TRUE(manager->Register(provider, report));
    const auto activated = manager->Activate(provider, report);
    ASSERT_FALSE(activated);
    EXPECT_EQ(activated.error(), MakeModuleError(ModuleError::kInvalidState));
    EXPECT_EQ(report.OriginalError, activated.error());
    EXPECT_EQ(manager->Inspect(provider)->State, ModuleState::kCleanupPending);
    EXPECT_FALSE(observer.Access(*a).TryBorrow());
    EXPECT_FALSE(observer.Access(*b).TryBorrow());
    EXPECT_FALSE(observer.Access(*c).TryBorrow());
    EXPECT_EQ(observer.Count(Event::kContextCreate), 2U);
    EXPECT_EQ(observer.Count(Event::kStop), 2U);
    EXPECT_EQ(observer.Count(Event::kDestroy), 2U);
    EXPECT_EQ(observer.NativeUnloadCount(), 0U);
    ASSERT_TRUE(manager->TryFinishUnload(provider, report));
    EXPECT_EQ(observer.NativeUnloadCount(), 1U);
    ASSERT_TRUE(manager->BeginCloseScope(*a, report));
    ASSERT_TRUE(manager->TryFinishCloseScope(*a, report));
    EXPECT_TRUE(manager->CreateScope(Scope::kWorld, *c, report));
}

TEST_F(SharedScopeLifecycle, TypedDependency)
{
    for (const bool wrongType : {false, true})
    {
        auto& observer = pg::test::Observer();
        observer.Reset();
        observer.ConfigureSharedFixture({.WrongScopeDependencyType = wrongType});
        RegistrationScope declarations;
        ModuleOperationReport report;
        auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
        ASSERT_TRUE(manager);
        const auto context = manager->CreateScope(Scope::kContext, manager->HostScope(), report);
        ASSERT_TRUE(context);
        const auto world = manager->CreateScope(Scope::kWorld, *context, report);
        ASSERT_TRUE(world);
        const auto loaded = Load(*manager, report, true);
        ASSERT_TRUE(loaded);
        const auto provider = loaded->Modules.front();
        const auto consumer = loaded->Modules.back();
        ASSERT_TRUE(manager->Register(provider, report));
        ASSERT_TRUE(manager->Activate(provider, report));
        ASSERT_TRUE(manager->Register(consumer, report));
        const auto activated = manager->Activate(consumer, report);
        if (wrongType)
        {
            ASSERT_FALSE(activated);
            const auto wrongType = activated;
            EXPECT_EQ(wrongType.error(), MakeModuleError(ModuleError::kTypeMismatch));
            EXPECT_TRUE(observer.Access(*world).TryBorrow());
        }
        else
        {
            ASSERT_TRUE(activated);
            EXPECT_EQ(observer.Count(Event::kConsumerStart), 1U);
        }
        if (activated)
        {
            ASSERT_TRUE(manager->BeginUnload(consumer, report));
        }
        ASSERT_TRUE(manager->TryFinishUnload(consumer, report));
        EXPECT_EQ(observer.Count(Event::kDependencyUsed), wrongType ? 0U : 1U);
        EXPECT_EQ(observer.Count(Event::kConsumerDestroy), wrongType ? 0U : 1U);
        EXPECT_EQ(observer.Count(Event::kConsumerStop), wrongType ? 0U : 1U);
        EXPECT_EQ(observer.Count(Event::kConsumerPoll), wrongType ? 0U : 1U);
        EXPECT_EQ(observer.Count(Event::kDeleterDestroy), wrongType ? 0U : 1U);
        ASSERT_TRUE(manager->BeginUnload(provider, report));
        ASSERT_TRUE(manager->TryFinishUnload(provider, report));
        EXPECT_EQ(observer.Count(Event::kFactoryDestroy), 4U);
        EXPECT_EQ(observer.Count(Event::kProviderDestroy), 2U);
        EXPECT_EQ(observer.NativeUnloadCount(), 2U);
    }
}
} // namespace
