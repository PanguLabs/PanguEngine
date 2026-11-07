#pragma once
#include <ModuleBuiltin.h>
#include <ModuleManager.h>
#include <RegistrationContext.h>

namespace pg::module::test
{
// 旧同二进制回归的装配入口：常驻根与业务批次分别接纳，不改变业务断言。
inline Result<ModuleManager> CreateManager(const StaticModuleCatalog& catalog, RegistrationScope& scope,
                                           std::span<const OptionalDependencyBinding> bindings,
                                           ModuleOperationReport& report)
{
    auto manager = ModuleManager::Create(scope, BuiltinResidentQueries(), {}, report);
    if (!manager)
    {
        return manager;
    }
    std::vector<ModuleLoadCandidate> candidates;
    for (const auto& entry : catalog.Entries)
    {
        candidates.emplace_back(StaticModuleCandidate{entry.Query});
    }
    auto loaded = manager->Load(candidates, bindings, report);
    if (!loaded)
    {
        return std::unexpected(loaded.error());
    }
    return manager;
}
inline std::vector<ModuleSnapshot> BusinessSnapshots(const ModuleManager& manager)
{
    std::vector<ModuleSnapshot> result;
    for (const auto handle : manager.Modules())
    {
        auto snapshot = manager.Inspect(handle);
        if (snapshot && !snapshot->Resident)
        {
            result.push_back(std::move(*snapshot));
        }
    }
    return result;
}
inline std::vector<ModuleId> BusinessOrder(const ModuleManager& manager)
{
    std::vector<ModuleId> result;
    for (const auto handle : manager.RegistrationOrder())
    {
        const auto snapshot = manager.Inspect(handle);
        if (snapshot && !snapshot->Resident)
        {
            result.push_back(snapshot->Descriptor.Id);
        }
    }
    return result;
}
} // namespace pg::module::test
