#include "../Fixtures/ScopeAccessFixture.h"
#include <ScopeAccess.h>
#include <benchmark/registration.h>
#include <benchmark/state.h>
#include <benchmark/utils.h>

namespace
{
void ScopeAccessBorrowRelease(benchmark::State& state)
{
    const pg::test::ScopeAccessFixture fixture;
    const auto access = fixture.Access();
    for (const auto iteration : state)
    {
        static_cast<void>(iteration);
        auto borrow = access.TryBorrow();
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
