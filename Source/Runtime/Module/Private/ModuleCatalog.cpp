#include "ModuleCatalog.h"
#include "Error.h"
#include "ModuleDescriptor.h"
#include "ModuleIdentity.h"
#include "ModuleManager.h"
#include "ModuleOperation.h"
#include "ModuleRecord.h"
#include "Result.h"
#include <ModuleError.h>
#include <algorithm>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace pg::module::detail
{
namespace
{
std::unexpected<Error> Reject(ModuleOperationReport& report, ModuleError code, ModuleId id = ModuleId(),
                              ModuleId related = ModuleId())
{
    const auto error = MakeModuleError(code);
    report = {.PrimaryError = error, .Module = id, .RelatedModule = related};
    return std::unexpected(error);
}
} // namespace
Status CopyDefinition(ModuleRecord& record, QueryModuleFn query, ModuleOperationReport& report) noexcept
{
    if (query == nullptr)
    {
        return Reject(report, ModuleError::kInvalidDefinition);
    }
    const auto* definition = query();
    if (definition == nullptr || definition->RegisterModule == nullptr)
    {
        return Reject(report, ModuleError::kInvalidDefinition);
    }
    const auto& descriptor = definition->Descriptor;
    if (!descriptor.Id.IsValid() || descriptor.ProtocolVersion != 1)
    {
        return Reject(report, ModuleError::kInvalidDescriptor, descriptor.Id);
    }
    if (descriptor.Dependencies.size() > kMaxModuleDependencies ||
        descriptor.DisplayName.size() > kMaxModuleTextBytes || descriptor.BuildIdentity.size() > kMaxModuleTextBytes)
    {
        return Reject(report, ModuleError::kLimitExceeded, descriptor.Id);
    }
    for (std::size_t index = 0; index < descriptor.Dependencies.size(); ++index)
    {
        const auto& dependency = descriptor.Dependencies.subspan(index).front();
        if (!dependency.Id.IsValid() || dependency.Id == descriptor.Id ||
            (dependency.Kind != DependencyKind::kRequired && dependency.Kind != DependencyKind::kOptional) ||
            std::ranges::any_of(descriptor.Dependencies.first(index),
                                [&](const auto& previous) { return previous.Id == dependency.Id; }))
        {
            return Reject(report, ModuleError::kInvalidDependency, descriptor.Id, dependency.Id);
        }
    }
    record.Descriptor = {
        .Id = descriptor.Id,
        .Version = descriptor.Version,
        .ProtocolVersion = descriptor.ProtocolVersion,
        .Dependencies = {descriptor.Dependencies.begin(), descriptor.Dependencies.end()},
        .DisplayName = std::string(descriptor.DisplayName),
        .BuildIdentity = std::string(descriptor.BuildIdentity),
    };
    record.RegisterModule = definition->RegisterModule;
    return {};
}

Result<std::vector<ModuleRecord*>> ResolveCatalog(std::span<ModuleRecord* const> existing,
                                                  std::span<ModuleRecord* const> added,
                                                  std::span<const OptionalDependencyBinding> selections,
                                                  ModuleOperationReport& report) noexcept
{
    std::vector<ModuleRecord*> all(existing.begin(), existing.end());
    all.insert(all.end(), added.begin(), added.end());
    const auto find = [&](ModuleId id) -> ModuleRecord*
    {
        const auto found = std::ranges::find_if(all, [&](const auto* record) { return record->Descriptor.Id == id; });
        return found == all.end() ? nullptr : *found;
    };
    for (std::size_t index = 0; index < all.size(); ++index)
    {
        if (std::ranges::any_of(std::span(all).first(index), [&](const auto* record)
                                { return record->Descriptor.Id == all.at(index)->Descriptor.Id; }))
        {
            return Reject(report, ModuleError::kDuplicateModule, all.at(index)->Descriptor.Id);
        }
    }
    for (std::size_t index = 0; index < selections.size(); ++index)
    {
        const auto& binding = selections.subspan(index).front();
        const auto dependent =
            std::ranges::find_if(added, [&](const auto* record) { return record->Descriptor.Id == binding.Dependent; });
        if (dependent == added.end() ||
            std::ranges::find(selections.first(index), binding) != selections.first(index).end())
        {
            return Reject(report, ModuleError::kInvalidOptionalBinding, binding.Dependent, binding.Dependency);
        }
        const auto& dependencies = (*dependent)->Descriptor.Dependencies;
        const auto found = std::ranges::find(dependencies, binding.Dependency, &ModuleDependency::Id);
        if (found == dependencies.end() || found->Kind != DependencyKind::kOptional)
        {
            return Reject(report, ModuleError::kInvalidOptionalBinding, binding.Dependent, binding.Dependency);
        }
    }
    for (auto* record : added)
    {
        for (const auto& dependency : record->Descriptor.Dependencies)
        {
            const bool selected =
                std::ranges::find(selections, OptionalDependencyBinding{.Dependent = record->Descriptor.Id,
                                                                        .Dependency = dependency.Id}) !=
                selections.end();
            const bool required = dependency.Kind == DependencyKind::kRequired;
            const auto* provider = find(dependency.Id);
            auto reason = DependencyBindingReason::kNotSelected;
            if (provider == nullptr || provider->State == ModuleState::kCleanupPending)
            {
                if (required || selected)
                {
                    return Reject(report, ModuleError::kMissingDependency, record->Descriptor.Id, dependency.Id);
                }
                reason = DependencyBindingReason::kMissing;
            }
            else if (provider->Descriptor.Version != dependency.Version)
            {
                if (required || selected)
                {
                    return Reject(report, ModuleError::kVersionMismatch, record->Descriptor.Id, dependency.Id);
                }
                reason = DependencyBindingReason::kVersionMismatch;
            }
            else if (required || selected)
            {
                reason = required ? DependencyBindingReason::kRequired : DependencyBindingReason::kSelected;
            }
            record->Bindings.push_back({.Dependency = dependency.Id, .Reason = reason});
        }
    }
    std::vector<ModuleRecord*> order;
    while (order.size() != all.size())
    {
        const auto before = order.size();
        for (auto* record : all)
        {
            if (std::ranges::find(order, record) != order.end())
            {
                continue;
            }
            if (std::ranges::all_of(record->Bindings,
                                    [&](const auto& binding)
                                    {
                                        return !binding.IsBound() ||
                                               std::ranges::any_of(
                                                   order, [&](const auto* done)
                                                   { return done->Descriptor.Id == binding.Dependency; });
                                    }))
            {
                order.push_back(record);
            }
        }
        if (before == order.size())
        {
            return Reject(report, ModuleError::kDependencyCycle);
        }
    }
    std::erase_if(order, [&](const auto* record) { return std::ranges::find(added, record) == added.end(); });
    return order;
}
} // namespace pg::module::detail
