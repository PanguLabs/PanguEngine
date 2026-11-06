#include "Fixtures/ScopeAccessFixture.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

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

} // namespace
