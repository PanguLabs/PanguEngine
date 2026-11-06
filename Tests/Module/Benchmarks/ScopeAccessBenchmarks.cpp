#include <ScopeAccess.h>
#include <benchmark/benchmark.h>
#include <memory>

namespace
{
void ScopeAccessBorrowRelease(benchmark::State& state)
{
    int value = 42;
    const auto control = std::make_shared<pg::module::detail::ScopeObjectControl>();
    control->Module = std::make_shared<pg::module::detail::ScopeGate>();
    control->Scope = std::make_shared<pg::module::detail::ScopeGate>();
    control->Object = &value;
    control->Type = pg::module::detail::ScopeTypeToken<int>();
    control->Module->Open();
    control->Scope->Open();
    control->Gate.Open();
    auto access = pg::module::detail::ScopeAccessMaker::Make<int>(control);
    if (!access)
    {
        state.SkipWithError("ScopeAccess setup failed");
        return;
    }
    for (const auto iteration : state)
    {
        static_cast<void>(iteration);
        auto borrow = access->TryBorrow();
        if (!borrow)
        {
            state.SkipWithError("TryBorrow failed");
            break;
        }
        benchmark::DoNotOptimize(**borrow);
        borrow->Release();
    }
}
BENCHMARK(ScopeAccessBorrowRelease);
} // namespace
