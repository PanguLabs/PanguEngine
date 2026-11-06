#include <ModuleDescriptor.h>
#include <ModuleIdentity.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <StaticModuleHost.h>
#include <array>
#include <benchmark/benchmark.h>
#include <cstddef>
#include <span>
#include <utility>

namespace
{
using namespace pg::module;
constexpr std::size_t kMaxModules = 32;
// Query 接口不携带上下文，串行测量通过稳定存储传递候选。
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::array<ModuleDefinition, kMaxModules> gDefinitions;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::array<ModuleDependency, kMaxModules> gDependencies;
pg::Status Register(RegistrationContext& /*context*/) noexcept { return {}; }
template <std::size_t Index>
const ModuleDefinition* Query() noexcept
{
    return &gDefinitions.at(Index);
}
template <std::size_t... Index>
constexpr auto Entries(std::index_sequence<Index...> /*indices*/)
{
    return std::array{StaticCatalogEntry{&Query<Index>}...};
}
constexpr auto kEntries = Entries(std::make_index_sequence<kMaxModules>{});
void CatalogCreate(benchmark::State& state)
{
    const auto count = static_cast<std::size_t>(state.range(0));
    for (std::size_t index = 0; index < count; ++index)
    {
        ModuleId id{};
        id.Bytes.front() = static_cast<std::byte>(index + 1);
        gDefinitions.at(index) = {
            .Descriptor =
                {
                    .Id = id,
                    .Version = {.Major = 1, .Minor = 0, .Patch = 0},
                    .Dependencies = {},
                    .DisplayName = "Benchmark",
                    .BuildIdentity = "benchmark",
                },
            .RegisterModule = &Register,
        };
        if (index > 0)
        {
            gDependencies.at(index) = {
                .Id = gDefinitions.at(index - 1).Descriptor.Id,
                .Version = {.Major = 1, .Minor = 0, .Patch = 0},
            };
            gDefinitions.at(index).Descriptor.Dependencies = std::span(&gDependencies.at(index), 1);
        }
    }
    const StaticModuleCatalog catalog{std::span(kEntries).first(count)};
    RegistrationScope scope;
    state.SetLabel("Create + Host destruction");
    for (const auto iteration : state)
    {
        static_cast<void>(iteration);
        ModuleOperationReport report;
        auto host = StaticModuleHost::Create(catalog, scope, {}, report);
        if (!host)
        {
            state.SkipWithError("StaticModuleHost::Create failed");
            break;
        }
        benchmark::DoNotOptimize(host->Modules().size());
        benchmark::ClobberMemory();
    }
}
BENCHMARK(CatalogCreate)->Arg(1)->Arg(8)->Arg(32);
} // namespace
