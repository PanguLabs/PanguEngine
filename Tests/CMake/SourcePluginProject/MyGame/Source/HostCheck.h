#pragma once

#include "FixtureModules.h"

#include <ModuleBuiltin.h>
#include <ModuleManager.h>
#include <RegistrationContext.h>
#include <ranges>
#include <span>
#include <vector>

namespace pg::fixture
{
[[nodiscard]] inline int VerifyCandidates(std::span<const module::ModuleLoadCandidate> candidates,
                                          module::ModuleId dependent, bool shared) noexcept
{
    module::RegistrationScope scope;
    module::ModuleOperationReport report;
    auto host = module::ModuleManager::Create(scope, module::BuiltinResidentQueries(), {}, report);
    if (!host || host->Modules().size() != 2)
    {
        return 1;
    }
    for (const auto root : host->RegistrationOrder())
    {
        if (!host->Register(root, report) || !host->Activate(root, report))
        {
            return 2;
        }
    }
    const auto loaded = host->Load(candidates, {}, report);
    if (!loaded || loaded->Modules.size() != 2 || host->Inspect(loaded->Modules.front())->Descriptor.Id != kPhysicsId ||
        host->Inspect(loaded->Modules.back())->Descriptor.Id != dependent)
    {
        return 3;
    }
    for (const auto handle : loaded->Modules)
    {
        const auto before = host->Inspect(handle);
        if (!before || before->Resident || before->State != module::ModuleState::kLoaded ||
            !host->Register(handle, report) || !host->Activate(handle, report))
        {
            return 4;
        }
        const auto after = host->Inspect(handle);
        if (!after || after->State != module::ModuleState::kActive)
        {
            return 5;
        }
    }
    if (scope.Declarations().size() != 2)
    {
        return 6;
    }
    // 业务沿 Manager 返回的依赖顺序逆序关闭，最后才关闭基础常驻根。
    for (const auto handle : loaded->Modules | std::views::reverse)
    {
        if (!host->BeginUnload(handle, report) || !host->TryFinishUnload(handle, report))
        {
            return 7;
        }
    }
    if (!host->BeginShutdown(report) || !host->TryFinishShutdown(report) || !scope.Declarations().empty())
    {
        return 8;
    }
    // 源形式也由调用者明确选择；不会把 OS 隐式加载当作 Manager 已接纳。
    return shared == std::holds_alternative<module::SharedModuleCandidate>(candidates.front()) ? 0 : 9;
}

[[nodiscard]] inline int VerifyCatalog(const module::StaticModuleCatalog& catalog, module::ModuleId dependent) noexcept
{
    std::vector<module::ModuleLoadCandidate> candidates;
    for (const auto& entry : catalog.Entries)
    {
        candidates.emplace_back(module::StaticModuleCandidate{entry.Query});
    }
    return VerifyCandidates(candidates, dependent, false);
}
} // namespace pg::fixture
