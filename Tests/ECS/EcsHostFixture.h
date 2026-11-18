#pragma once

#include <EcsModule.h>
#include <EngineHost.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <LogConfig.h>
#include <ModuleDescriptor.h>
#include <Result.h>
#include <array>

namespace pg::ecs::test
{
[[nodiscard]] inline Status StartEcsHost(engine::EngineHost& host, engine::HostOperationReport& report) noexcept
{
    const std::array<module::QueryModuleFn, 1> residentQueries{QueryEcsModule};
    const engine::HostStartupConfig config{
        .AdditionalResidentQueries = residentQueries,
        .ResidentBindings = {},
        .Modules = {},
        .ModuleBindings = {},
        .Logging =
            {
                .Directory = {},
                .Enabled = log::OutputMask::kNone,
                .Required = log::OutputMask::kNone,
            },
    };
    return host.Start(config, report);
}
} // namespace pg::ecs::test
