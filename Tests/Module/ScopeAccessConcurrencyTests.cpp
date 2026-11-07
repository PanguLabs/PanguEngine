#include "Fixtures/ScopeAccessFixture.h"
#include "Support/ScopeTestSupport.h"

#include <ScopeAccess.h>
#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <thread>
#include <utility>

namespace
{
using pg::test::ScopeAccessFixture;

TEST(ScopeAccessConcurrency, AcquisitionAndCloseAreLinearized)
{
    for (int round = 0; round < 200; ++round)
    {
        ScopeAccessFixture const fixture;
        const auto access = fixture.Access();
        std::atomic<bool> start{false};
        std::atomic<bool> closed{false};
        std::atomic<bool> failed{false};
        std::jthread reader(
            [&]
            {
                while (!start.load())
                {
                    std::this_thread::yield();
                }
                auto before = access.TryBorrow();
                if (before && **before != 42)
                {
                    failed = true;
                }
                while (!closed.load())
                {
                    std::this_thread::yield();
                }
                if (access.TryBorrow())
                {
                    failed = true;
                }
            });
        start = true;
        fixture.Control->Gate.Close();
        closed = true;
        reader.join();
        EXPECT_FALSE(failed.load()) << "round=" << round;
        EXPECT_EQ(fixture.Control->Gate.Count(), 0) << "round=" << round;
        EXPECT_EQ(fixture.Control->Module->Count(), 0) << "round=" << round;
        EXPECT_EQ(fixture.Control->Scope->Count(), 0) << "round=" << round;
    }
}

TEST(ScopeAccessConcurrency, SuccessfulAcquisitionPrecedesCloseAndReleaseCrossesThreads)
{
    ScopeAccessFixture const fixture;
    const auto access = fixture.Access();
    std::barrier ready(2);
    std::barrier closed(2);
    std::atomic<bool> admitted{false};
    std::jthread worker(
        [&]
        {
            auto borrow = access.TryBorrow();
            admitted = borrow.has_value();
            ready.arrive_and_wait();
            closed.arrive_and_wait();
            if (borrow)
            {
                borrow->Release();
            }
        });
    ready.arrive_and_wait();
    EXPECT_TRUE(admitted.load());
    EXPECT_EQ(fixture.Control->Gate.Count(), 1U);
    fixture.Control->Gate.Close();
    EXPECT_FALSE(access.TryBorrow());
    closed.arrive_and_wait();
    worker.join();
    EXPECT_EQ(fixture.Control->Gate.Count(), 0U);
    EXPECT_EQ(fixture.Control->Scope->Count(), 0U);
    EXPECT_EQ(fixture.Control->Module->Count(), 0U);

    ScopeAccessFixture const second;
    auto borrow = second.Access().TryBorrow();
    ASSERT_TRUE(borrow);
    std::jthread releaser([](pg::module::ScopeBorrow<int> guard) { guard.Release(); }, std::move(*borrow));
    releaser.join();
    EXPECT_EQ(second.Control->Gate.Count(), 0U);
    EXPECT_EQ(second.Control->Scope->Count(), 0U);
    EXPECT_EQ(second.Control->Module->Count(), 0U);
}

TEST(ScopeAccessConcurrency, ObserverCountsEventsFromMultipleThreads)
{
    auto& observer = pg::test::Observer();
    observer.Reset();
    const auto record = [&]
    {
        for (int i = 0; i < 100; ++i)
        {
            observer.Record(pg::test::Event::kRegister);
            observer.Record(pg::test::Event::kNativeUnload);
        }
    };
    std::jthread first(record);
    std::jthread second(record);
    first.join();
    second.join();
    EXPECT_EQ(observer.RegisterCount(), 200U);
    EXPECT_EQ(observer.NativeUnloadCount(), 200U);
    EXPECT_EQ(observer.Events().size(), 400U);
    observer.Reset();
}

} // namespace
