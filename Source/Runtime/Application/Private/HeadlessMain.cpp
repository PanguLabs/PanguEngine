#include <PanguApplication.h>

#include <EngineHost.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <LogTypes.h>

#include <cstdio>
#include <cstdlib>

int main()
{
    pg::engine::EngineHost host;
    pg::engine::HostOperationReport report;
    const pg::engine::HostStartupConfig config{
        .AdditionalResidentQueries = {},
        .ResidentBindings = {},
        .Modules = {},
        .ModuleBindings = {},
        .Logging = {.Directory = {}, .Enabled = pg::log::OutputMask::kNone},
    };
    if (!host.Start(config, report))
    {
        std::fputs("EngineHost startup failed\n", stderr);
        return EXIT_FAILURE;
    }

    const auto tool = pg::app::RunTool(host);
    const auto begun = host.BeginShutdown(report);
    const auto closed = host.TryFinishShutdown(report);
    if (!begun || !closed || host.GetState() != pg::engine::HostState::kClosed)
    {
        std::fputs("EngineHost shutdown failed\n", stderr);
        return EXIT_FAILURE;
    }
    if (!tool)
    {
        std::fputs("RunTool failed\n", stderr);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
