#include "ModuleOperation.h"
#include "SharedFixtureContract.h"
#include "SharedFixturePaths.h"
#include <ModuleBuiltin.h>
#include <ModuleCodePin.h>
#include <ModuleManager.h>
#include <RegistrationContext.h>
#include <ScopeTestSupport.h>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <barrier>
#include <thread>
#include <utility>

namespace pg::module
{
using namespace fixture;

TEST(ModuleUnloadConcurrency, PinVersusCloseHasOnlyAcceptedOrRejectedOutcome)
{
    for (int iteration = 0; iteration != 64; ++iteration)
    {
        auto& observer = pg::test::Observer();
        observer.Reset();
        RegistrationScope declarations;
        ModuleOperationReport report;
        auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
        ASSERT_TRUE(manager);
        const std::array candidates{
            ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedProviderId, .Path = kSharedProviderPath}},
        };
        auto loaded = manager->Load(candidates, {}, report);
        ASSERT_TRUE(loaded);
        const auto handle = loaded->Modules.front();
        ASSERT_TRUE(manager->Register(handle, report));
        ASSERT_TRUE(manager->Activate(handle, report));
        std::barrier start(2);
        ModuleCodePin pin;
        std::thread worker(
            [&]
            {
                start.arrive_and_wait();
                auto acquired = manager->TryPinForBusiness(handle);
                if (acquired)
                {
                    pin = std::move(*acquired);
                }
            });
        start.arrive_and_wait();
        EXPECT_TRUE(manager->BeginUnload(handle, report));
        worker.join();
        EXPECT_FALSE(manager->TryPinForBusiness(handle));
        if (pin)
        {
            EXPECT_FALSE(manager->TryFinishUnload(handle, report));
            EXPECT_EQ(observer.NativeUnloadCount(), 0U);
            std::thread release{[owned = std::move(pin)] mutable { owned.Release(); }};
            release.join();
            EXPECT_EQ(observer.NativeUnloadCount(), 0U);
        }
        ASSERT_TRUE(manager->TryFinishUnload(handle, report));
        EXPECT_EQ(observer.NativeUnloadCount(), 1U);
        EXPECT_FALSE(manager->TryPinForBusiness(handle));
    }
}

TEST(ModuleUnloadConcurrency, StaleHandleCannotPinConcurrentLoadsAndSlotReuse)
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    const std::array candidates{
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedProviderId, .Path = kSharedProviderPath}},
    };
    auto loaded = manager->Load(candidates, {}, report);
    ASSERT_TRUE(loaded);
    const auto stale = loaded->Modules.front();
    ASSERT_TRUE(manager->Register(stale, report));
    ASSERT_TRUE(manager->Activate(stale, report));
    ASSERT_TRUE(manager->BeginUnload(stale, report));
    ASSERT_TRUE(manager->TryFinishUnload(stale, report));
    std::atomic<bool> finished{};
    std::atomic<unsigned> accepted{};
    std::atomic<unsigned> attempts{};
    std::barrier start(2);
    std::thread worker(
        [&]
        {
            start.arrive_and_wait();
            while (!finished.load(std::memory_order_acquire))
            {
                if (manager->TryPinForBusiness(stale))
                {
                    accepted.fetch_add(1, std::memory_order_relaxed);
                }
                attempts.fetch_add(1, std::memory_order_relaxed);
            }
        });
    start.arrive_and_wait();
    bool valid = true;
    for (int iteration = 0; iteration != 128; ++iteration)
    {
        auto next = manager->Load(candidates, {}, report);
        if (!next)
        {
            valid = false;
            break;
        }
        const auto handle = next->Modules.front();
        valid &= handle.Slot == stale.Slot && handle.Generation > stale.Generation;
        valid &= manager->Register(handle, report).has_value();
        valid &= manager->Activate(handle, report).has_value();
        valid &= manager->TryPinForBusiness(handle).has_value();
        valid &= manager->BeginUnload(handle, report).has_value();
        valid &= manager->TryFinishUnload(handle, report).has_value();
    }
    finished.store(true, std::memory_order_release);
    worker.join();
    EXPECT_TRUE(valid);
    EXPECT_GT(attempts.load(), 0U);
    EXPECT_EQ(accepted.load(), 0U);
}
} // namespace pg::module
