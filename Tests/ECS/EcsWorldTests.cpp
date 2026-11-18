#include <PanguFlecs.h> // IWYU pragma: keep

#include "EcsHostFixture.h"
#include <EcsError.h>
#include <EngineHost.h>
#include <HostOperation.h>
#include <ModuleError.h>
#include <World.h>
#include <WorldAccess.h>

#include <array>
#include <gtest/gtest.h>
#include <limits>
#include <type_traits>
#include <vector>

namespace pg::ecs::test
{
namespace
{
struct Position
{
    int Value{};
};

// NOLINTNEXTLINE(misc-include-cleaner) PanguFlecs.h 提供未标注重导出的 flecs C++ 接口。
[[nodiscard]] std::vector<int> ReadPositions(const pg::flecs::world& native)
{
    const auto query = native.query<const Position>();
    std::vector<int> values;
    query.each([&values](const Position& position) { values.push_back(position.Value); });
    return values;
}

TEST(EcsWorld, NativeAccessAndIsolation)
{
    // NOLINTBEGIN(misc-include-cleaner) PanguFlecs.h 提供未标注重导出的 flecs C++ 接口。
    static_assert(std::is_same_v<pg::flecs::entity, ::flecs::entity>);
    static_assert(std::is_same_v<pg::flecs::query<const Position>, ::flecs::query<const Position>>);
    static_assert(std::is_same_v<decltype(pg::flecs::log::get_level()), int>);
    // NOLINTEND(misc-include-cleaner)

    engine::EngineHost host;
    engine::EngineHost otherHost;
    engine::HostOperationReport report;
    ASSERT_TRUE(StartEcsHost(host, report));
    ASSERT_TRUE(StartEcsHost(otherHost, report));
    const auto context = host.CreateContext(report);
    const auto otherContext = otherHost.CreateContext(report);
    ASSERT_TRUE(context);
    ASSERT_TRUE(otherContext);
    const auto first = host.CreateWorld(*context, report);
    const auto second = host.CreateWorld(*context, report);
    const auto foreign = otherHost.CreateWorld(*otherContext, report);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(foreign);
    EXPECT_FALSE(AcquireWorld(otherHost, *first));
    EXPECT_FALSE(AcquireWorld(host, *context));

    const auto firstAccess = AcquireWorld(host, *first);
    const auto secondAccess = AcquireWorld(host, *second);
    const auto foreignAccess = AcquireWorld(otherHost, *foreign);
    ASSERT_TRUE(firstAccess);
    ASSERT_TRUE(secondAccess);
    ASSERT_TRUE(foreignAccess);
    auto firstBorrow = firstAccess->TryBorrow();
    auto secondBorrow = secondAccess->TryBorrow();
    auto foreignBorrow = foreignAccess->TryBorrow();
    ASSERT_TRUE(firstBorrow);
    ASSERT_TRUE(secondBorrow);
    ASSERT_TRUE(foreignBorrow);
    const auto& firstNative = (*firstBorrow)->Native();
    const auto& secondNative = (*secondBorrow)->Native();
    const auto& foreignNative = (*foreignBorrow)->Native();
    firstNative.component<Position>();
    secondNative.component<Position>();
    foreignNative.component<Position>();
    firstNative.entity().set(Position{7});
    secondNative.entity().set(Position{11});
    foreignNative.entity().set(Position{13});
    EXPECT_EQ(ReadPositions(firstNative), std::vector<int>{7});
    EXPECT_EQ(ReadPositions(secondNative), std::vector<int>{11});
    firstBorrow->Release();
    ASSERT_TRUE(host.BeginCloseScope(*first, report));
    ASSERT_TRUE(host.TryFinishCloseScope(*first, report));
    EXPECT_FALSE(AcquireWorld(host, *first));
    EXPECT_FALSE(firstAccess->TryBorrow());
    EXPECT_EQ(ReadPositions(secondNative), std::vector<int>{11});
    secondBorrow->Release();
    ASSERT_TRUE(host.BeginShutdown(report));
    ASSERT_TRUE(host.TryFinishShutdown(report));
    EXPECT_EQ(ReadPositions(foreignNative), std::vector<int>{13});
}

TEST(EcsWorld, ProgressValidatesDelta)
{
    int ticks{};
    double observedDelta{};
    engine::EngineHost host;
    engine::HostOperationReport report;
    ASSERT_TRUE(StartEcsHost(host, report));
    const auto context = host.CreateContext(report);
    ASSERT_TRUE(context);
    const auto world = host.CreateWorld(*context, report);
    ASSERT_TRUE(world);
    const auto access = AcquireWorld(host, *world);
    ASSERT_TRUE(access);
    const auto borrow = access->TryBorrow();
    ASSERT_TRUE(borrow);
    const auto& native = (*borrow)->Native();
    native.component<Position>();
    native.entity().set(Position{});
    native.system<const Position>().run(
        // NOLINTNEXTLINE(misc-include-cleaner) PanguFlecs.h 提供未标注重导出的 flecs C++ 接口。
        [&](pg::flecs::iter& it)
        {
            while (it.next())
            {
                ++ticks;
                observedDelta = static_cast<double>(it.delta_time());
            }
        });

    using Limits = std::numeric_limits<double>;
    constexpr std::array kInvalidSteps{
        0.0, -0.0, -1.0, Limits::quiet_NaN(), Limits::infinity(), Limits::max(), Limits::denorm_min(),
    };
    for (const auto delta : kInvalidSteps)
    {
        const auto result = (*borrow)->Progress(delta);
        ASSERT_FALSE(result) << delta;
        EXPECT_EQ(result.error(), MakeEcsError(EcsError::kInvalidDeltaTime));
        EXPECT_EQ(ticks, 0);
    }
    const auto advanced = (*borrow)->Progress(0.25);
    ASSERT_TRUE(advanced);
    EXPECT_TRUE(*advanced);
    EXPECT_EQ(ticks, 1);
    EXPECT_DOUBLE_EQ(observedDelta, 0.25);
}

TEST(EcsWorld, QuitIsLocal)
{
    int quitTicks{};
    int otherTicks{};
    engine::EngineHost host;
    engine::HostOperationReport report;
    ASSERT_TRUE(StartEcsHost(host, report));
    const auto context = host.CreateContext(report);
    ASSERT_TRUE(context);
    const auto first = host.CreateWorld(*context, report);
    const auto other = host.CreateWorld(*context, report);
    ASSERT_TRUE(first);
    ASSERT_TRUE(other);
    const auto firstAccess = AcquireWorld(host, *first);
    const auto otherAccess = AcquireWorld(host, *other);
    ASSERT_TRUE(firstAccess);
    ASSERT_TRUE(otherAccess);
    const auto firstBorrow = firstAccess->TryBorrow();
    const auto otherBorrow = otherAccess->TryBorrow();
    ASSERT_TRUE(firstBorrow);
    ASSERT_TRUE(otherBorrow);

    const auto& firstNative = (*firstBorrow)->Native();
    firstNative.component<Position>();
    firstNative.entity().set(Position{});
    firstNative.system<const Position>().each(
        // NOLINTNEXTLINE(misc-include-cleaner) PanguFlecs.h 提供未标注重导出的 flecs C++ 接口。
        [&](pg::flecs::entity entity, const Position&)
        {
            ++quitTicks;
            entity.world().quit();
        });
    const auto& otherNative = (*otherBorrow)->Native();
    otherNative.component<Position>();
    otherNative.entity().set(Position{});
    otherNative.system<const Position>().each([&](const Position&) { ++otherTicks; });

    const auto quit = (*firstBorrow)->Progress(0.25);
    const auto advanced = (*otherBorrow)->Progress(0.25);
    ASSERT_TRUE(quit);
    ASSERT_TRUE(advanced);
    EXPECT_FALSE(*quit);
    EXPECT_TRUE(*advanced);
    EXPECT_EQ(quitTicks, 1);
    EXPECT_EQ(otherTicks, 1);
}

TEST(EcsWorld, QueryCacheDelaysClose)
{
    engine::EngineHost host;
    engine::HostOperationReport report;
    ASSERT_TRUE(StartEcsHost(host, report));
    const auto context = host.CreateContext(report);
    ASSERT_TRUE(context);
    const auto world = host.CreateWorld(*context, report);
    ASSERT_TRUE(world);
    const auto access = AcquireWorld(host, *world);
    ASSERT_TRUE(access);
    auto borrow = access->TryBorrow();
    ASSERT_TRUE(borrow);
    {
        const auto& native = (*borrow)->Native();
        native.component<Position>();
        native.entity().set(Position{7});
        auto cachedQuery = native.query_builder<const Position>().cached().build();
        ASSERT_TRUE(cachedQuery);
        const auto queryEntity = cachedQuery.entity();
        ASSERT_TRUE(queryEntity.is_alive());
        EXPECT_EQ(cachedQuery.count(), 1);

        ASSERT_TRUE(host.BeginCloseScope(*world, report));
        EXPECT_FALSE(access->TryBorrow());
        const auto busy = host.TryFinishCloseScope(*world, report);
        ASSERT_FALSE(busy);
        EXPECT_EQ(busy.error(), module::MakeModuleError(module::ModuleError::kBusy));
        const auto stopping = (*borrow)->Progress(0.25);
        ASSERT_FALSE(stopping);
        EXPECT_EQ(stopping.error(), MakeEcsError(EcsError::kWorldStopping));

        cachedQuery.destruct();
        EXPECT_FALSE(queryEntity.is_alive());
    }
    borrow->Release();
    ASSERT_TRUE(host.TryFinishCloseScope(*world, report));
    EXPECT_FALSE(access->TryBorrow());
}
} // namespace
} // namespace pg::ecs::test
