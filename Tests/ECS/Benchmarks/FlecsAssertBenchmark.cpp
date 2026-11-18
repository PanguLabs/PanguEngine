#include <PanguFlecs.h> // IWYU pragma: keep
#include <benchmark/registration.h>
#include <benchmark/state.h>
#include <benchmark/utils.h>

namespace
{
constexpr int kEntityCount = 10'000;
constexpr int kWarmupIterations = 128;
constexpr int kSampleIterations = 8'192;
constexpr float kStep = 0.016F;

struct Position
{
    double Value{};
};

void FlecsProgressAndTypedQuery(benchmark::State& state)
{
    // PanguFlecs.h 提供未标注重导出的 flecs C++ 接口。
    const pg::flecs::world world; // NOLINT(misc-include-cleaner)
    world.component<Position>();
    world.system<Position>().each([](Position& position) { position.Value += 1.0; });
    for (int index = 0; index < kEntityCount; ++index)
    {
        world.entity().set(Position{});
    }
    const auto query = world.query<const Position>();
    const auto runFrame = [&world, &query]
    {
        world.progress(kStep);
        double total{};
        query.each([&total](const Position& position) { total += position.Value; });
        benchmark::DoNotOptimize(total);
    };

    for (int frame = 0; frame < kWarmupIterations; ++frame)
    {
        runFrame();
    }
    for (const auto iteration : state)
    {
        static_cast<void>(iteration);
        runFrame();
    }
}
BENCHMARK(FlecsProgressAndTypedQuery)->Iterations(kSampleIterations);
} // namespace
