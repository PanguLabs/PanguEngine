#include "FixtureModules.h"
#include "HostCheck.h"
#ifdef PANGU_FIXTURE_MODULAR
#include "ModuleCandidates.h"
#else
#include <GeneratedModuleCatalog.h>
#endif
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <span>

int main(int argc, char* argv[])
{
    const std::span arguments(argv, static_cast<std::size_t>(argc));
    const auto directory = std::filesystem::absolute(arguments.front()).parent_path() / "Logs" / "MyGame";
    const auto logging = pg::fixture::ApplicationLogging(directory, std::byte{0x21});
#ifdef PANGU_FIXTURE_MODULAR
    const int result =
        pg::fixture::VerifyCandidates(pg::fixture::ConsumerCandidates(), pg::fixture::kGameplayId, true, logging);
#else
    const int result =
        pg::fixture::VerifyCatalog(pg::module::GeneratedStaticModuleCatalog(), pg::fixture::kGameplayId, logging);
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
