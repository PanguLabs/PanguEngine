#include "FixtureModules.h"
#include "HostCheck.h"
#ifdef PANGU_FIXTURE_MODULAR
#include "ModuleCandidates.h"
#else
#include <GeneratedModuleCatalog.h>
#endif
#include <cstdio>

int main()
{
#ifdef PANGU_FIXTURE_MODULAR
    const int result = pg::fixture::VerifyCandidates(pg::fixture::ConsumerCandidates(), pg::fixture::kGameplayId, true);
#else
    const int result = pg::fixture::VerifyCatalog(pg::module::GeneratedStaticModuleCatalog(), pg::fixture::kGameplayId);
#endif
    if (result == 0)
    {
        std::puts("Activated Physics Gameplay; shutdown complete");
    }
    else if (result == 3)
    {
        std::puts("EngineHost candidate start failed");
    }
    return result;
}
