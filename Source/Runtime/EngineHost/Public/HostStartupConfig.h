#pragma once

#include <LogConfig.h>
#include <ModuleManager.h>
#include <span>

namespace pg::engine
{
struct HostStartupConfig
{
    std::span<const module::QueryModuleFn> AdditionalResidentQueries;
    std::span<const module::OptionalDependencyBinding> ResidentBindings;
    std::span<const module::ModuleLoadCandidate> Modules;
    std::span<const module::OptionalDependencyBinding> ModuleBindings;
    log::LogConfig Logging;
};
} // namespace pg::engine
