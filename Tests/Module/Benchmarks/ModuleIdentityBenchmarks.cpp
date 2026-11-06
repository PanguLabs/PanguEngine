#include <ModuleIdentity.h>
#include <ModuleIdentityParse.h>
#include <benchmark/benchmark.h>
#include <string_view>

namespace
{
void ModuleIdentityParse(benchmark::State& state)
{
    std::string_view input = "0123456789abcdef0123456789abcdef";
    for (const auto iteration : state)
    {
        static_cast<void>(iteration);
        benchmark::DoNotOptimize(input);
        auto parsed = pg::module::detail::ParseModuleId(input);
        if (!parsed)
        {
            state.SkipWithError("ParseModuleId failed");
            break;
        }
        benchmark::DoNotOptimize(*parsed);
    }
}
void ModuleIdentityFormat(benchmark::State& state)
{
    auto parsed = pg::module::detail::ParseModuleId("0123456789abcdef0123456789abcdef");
    if (!parsed)
    {
        state.SkipWithError("ParseModuleId setup failed");
        return;
    }
    for (const auto iteration : state)
    {
        static_cast<void>(iteration);
        benchmark::DoNotOptimize(*parsed);
        auto formatted = pg::module::FormatModuleId(*parsed);
        benchmark::DoNotOptimize(formatted);
    }
}
BENCHMARK(ModuleIdentityParse);
BENCHMARK(ModuleIdentityFormat);
} // namespace
