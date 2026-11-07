#include "EngineHostTestFixture.h"
#include "HostServiceModule.h"
#include <EngineHost.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <HostTestState.h>
#include <ModuleError.h>
#include <barrier>
#include <cstddef>
#include <gtest/gtest.h>
#include <latch>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace pg::engine::test
{
TEST(EngineHostConcurrency, QueriedBorrowLinearizesWithClose)
{
    const auto state = std::make_shared<HostTestState>();
    SetHostTestState(state);
    EngineHost host;
    HostOperationReport report;
    const auto candidates = HostCandidates();
    HostStartupConfig config;
    config.Modules = candidates;
    ASSERT_TRUE(host.Start(config, report));
    const auto context = host.CreateContext(report);
    ASSERT_TRUE(context);
    const auto access = AcquireContextValue(host, *context);
    ASSERT_TRUE(access);
    std::barrier race(2);
    std::latch borrowed(1);
    std::latch closed(1);
    std::latch release(1);
    std::latch releaseInitial(1);
    std::latch initialReleased(1);
    bool heldBorrow{};
    bool validConcurrentResults{true};
    std::size_t retainedCount{};
    bool rejectedAfterClose{true};
    const auto managerThread = std::this_thread::get_id();
    bool unloadedOnManager{true};
    state->OnNativeUnload = [&] { unloadedOnManager = std::this_thread::get_id() == managerThread; };
    std::jthread worker(
        [&]
        {
            auto held = access->TryBorrow();
            std::vector<decltype(held)> retained;
            heldBorrow = held && (*held)->Value == 2;
            borrowed.count_down();
            race.arrive_and_wait();
            // 与 BeginShutdown 同时开始的调用可在线性化点任一侧完成。
            for (int attempt = 0; attempt < 256; ++attempt)
            {
                auto concurrent = access->TryBorrow();
                if (concurrent)
                {
                    validConcurrentResults = validConcurrentResults && (*concurrent)->Value == 2;
                    retained.push_back(std::move(concurrent));
                }
                else
                {
                    validConcurrentResults =
                        validConcurrentResults &&
                        concurrent.error() == module::MakeModuleError(module::ModuleError::kNotReady);
                }
            }
            closed.wait();
            for (int attempt = 0; attempt < 256; ++attempt)
            {
                const auto denied = access->TryBorrow();
                rejectedAfterClose = rejectedAfterClose && !denied &&
                                     denied.error() == module::MakeModuleError(module::ModuleError::kNotReady);
            }
            releaseInitial.wait();
            if (held)
            {
                held->Release();
            }
            retainedCount = retained.size();
            initialReleased.count_down();
            release.wait();
        });
    borrowed.wait();
    EXPECT_TRUE(heldBorrow);
    race.arrive_and_wait();
    EXPECT_TRUE(host.BeginShutdown(report));
    closed.count_down();
    const auto pending = host.TryFinishShutdown(report);
    EXPECT_FALSE(pending);
    if (!pending)
    {
        EXPECT_EQ(pending.error(), module::MakeModuleError(module::ModuleError::kBusy));
    }
    EXPECT_EQ(state->NativeUnloadCount, 0U);
    releaseInitial.count_down();
    initialReleased.wait();
    // 调度可以使竞态调用全部失败；若成功过，其 Borrow 必须独立阻止关闭。
    if (retainedCount != 0U)
    {
        const auto retainedPending = host.TryFinishShutdown(report);
        EXPECT_FALSE(retainedPending);
        if (!retainedPending)
        {
            EXPECT_EQ(retainedPending.error(), module::MakeModuleError(module::ModuleError::kBusy));
        }
        EXPECT_EQ(state->NativeUnloadCount, 0U);
    }
    release.count_down();
    worker.join();
    EXPECT_TRUE(rejectedAfterClose);
    EXPECT_TRUE(validConcurrentResults);
    EXPECT_EQ(state->NativeUnloadCount, 0U);
    EXPECT_TRUE(host.TryFinishShutdown(report));
    EXPECT_EQ(host.GetState(), HostState::kClosed);
#ifdef PG_TEST_MODULAR
    EXPECT_EQ(state->NativeUnloadCount, 1U);
#endif
    EXPECT_TRUE(unloadedOnManager);
    state->OnNativeUnload = {};
    auto residual = *access;
    const auto denied = residual.TryBorrow();
    ASSERT_FALSE(denied);
    EXPECT_EQ(denied.error(), module::MakeModuleError(module::ModuleError::kNotReady));
    residual = {};
}
} // namespace pg::engine::test
