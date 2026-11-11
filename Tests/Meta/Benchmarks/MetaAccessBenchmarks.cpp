#include "EngineHost.h"
#include "HostOperation.h"
#include "MetaActor.gen.h"
#include "MetaActor.h"
#include "MetaTestFixture.h"
#include "benchmark/registration.h"
#include "benchmark/state.h"
#include "benchmark/utils.h"
#include <HostMetaAccess.h>

namespace pg::test::meta_fixture
{
namespace
{
void DirectAccess(benchmark::State& state)
{
    Actor actor;
    for (const auto iteration : state)
    {
        (void)iteration;
        benchmark::DoNotOptimize(actor);
        actor.Health = 42;
        auto value = actor.Health;
        benchmark::DoNotOptimize(value);
        benchmark::ClobberMemory();
    }
}
void GeneratedAccess(benchmark::State& state)
{
    Actor actor;
    const auto& field = std::get<1>(meta::TypeTraits<Actor>::Fields);
    for (const auto iteration : state)
    {
        (void)iteration;
        benchmark::DoNotOptimize(actor);
        field.Write(actor, 42);
        auto value = field.Read(actor);
        benchmark::DoNotOptimize(value);
        benchmark::ClobberMemory();
    }
}
void DescriptionColdQuery(benchmark::State& state)
{
    engine::EngineHost host;
    engine::HostOperationReport report;
    if (!Start(host, report))
    {
        state.SkipWithError("Start failed");
        return;
    }
    for (const auto iteration : state)
    {
        (void)iteration;
        // 每次计入 Host/Module Gate、RTTI 查询、Pin 获取及释放，不缓存描述绕过保护。
        auto info = meta::BorrowTypeInfo<Actor>(host);
        if (!info)
        {
            state.SkipWithError("BorrowTypeInfo failed");
            break;
        }
        benchmark::DoNotOptimize(info->Get()->Fields.data());
        info->Release();
    }
    if (!Shutdown(host, report))
    {
        state.SkipWithError("Shutdown failed");
    }
}
BENCHMARK(DirectAccess);
BENCHMARK(GeneratedAccess);
BENCHMARK(DescriptionColdQuery);
} // namespace
} // namespace pg::test::meta_fixture
