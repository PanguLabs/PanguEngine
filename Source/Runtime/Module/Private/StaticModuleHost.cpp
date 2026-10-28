#include <StaticModuleHost.h>

#include <Error.h>
#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <ModuleIdentity.h>
#include <RegistrationContext.h>
#include <Result.h>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <iterator>
#include <optional>
#include <span>
#include <string>

namespace pg::module
{
namespace
{
std::unexpected<pg::Error> Reject(ModuleOperationReport& report, ModuleError code, ModuleId module = ModuleId(),
                                  ModuleId related = ModuleId()) noexcept
{
    const auto error = MakeModuleError(code);
    report = {
        .Progress = ModuleOperationProgress::kUnchanged,
        .PrimaryError = error,
        .Module = module,
        .RelatedModule = related,
    };
    return std::unexpected(error);
}

pg::Status ValidateDescriptor(const ModuleDescriptor& descriptor, ModuleOperationReport& report) noexcept
{
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
        const auto previous = descriptor.Dependencies.first(index);
        if (!dependency.Id.IsValid() || dependency.Id == descriptor.Id ||
            (dependency.Kind != DependencyKind::kRequired && dependency.Kind != DependencyKind::kOptional) ||
            std::ranges::any_of(previous, [&](const auto& entry) { return entry.Id == dependency.Id; }))
        {
            return Reject(report, ModuleError::kInvalidDependency, descriptor.Id, dependency.Id);
        }
    }
    return {};
}
} // namespace

StaticModuleHost::StaticModuleHost(RegistrationScope& scope) noexcept
    : Scope(&scope)
{
}

std::optional<std::size_t> StaticModuleHost::Find(ModuleId id) const noexcept
{
    const auto found = std::ranges::find(Records, id, [](const auto& record) { return record.Descriptor.Id; });
    if (found == Records.end())
    {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - Records.begin());
}

pg::Result<StaticModuleHost> StaticModuleHost::Create(const StaticModuleCatalog& catalog, RegistrationScope& scope,
                                                      std::span<const OptionalDependencyBinding> optionalBindings,
                                                      ModuleOperationReport& report) noexcept
{
    report = {};
    if (catalog.Entries.size() > kMaxCatalogModules ||
        optionalBindings.size() > kMaxCatalogModules * kMaxModuleDependencies)
    {
        return Reject(report, ModuleError::kLimitExceeded);
    }
    StaticModuleHost host(scope);
    host.Records.reserve(catalog.Entries.size());
    host.Callbacks.reserve(catalog.Entries.size());
    for (const auto& entry : catalog.Entries)
    {
        if (entry.Query == nullptr)
        {
            return Reject(report, ModuleError::kInvalidDefinition);
        }
        const auto* definition = entry.Query();
        if (definition == nullptr || definition->RegisterModule == nullptr ||
            definition->LinkedStaticModules != nullptr)
        {
            return Reject(report, ModuleError::kInvalidDefinition);
        }
        const auto& descriptor = definition->Descriptor;
        const auto valid = ValidateDescriptor(descriptor, report);
        if (!valid)
        {
            return std::unexpected(valid.error());
        }
        if (host.Find(descriptor.Id))
        {
            return Reject(report, ModuleError::kDuplicateModule, descriptor.Id);
        }
        host.Records.push_back({
            .Descriptor =
                {
                    .Id = descriptor.Id,
                    .Version = descriptor.Version,
                    .ProtocolVersion = descriptor.ProtocolVersion,
                    .Dependencies = {descriptor.Dependencies.begin(), descriptor.Dependencies.end()},
                    .DisplayName = std::string(descriptor.DisplayName),
                    .BuildIdentity = std::string(descriptor.BuildIdentity),
                },
            .Bindings = {},
        });
        host.Callbacks.push_back(definition->RegisterModule);
    }
    const auto resolved = host.Resolve(optionalBindings, report);
    if (!resolved)
    {
        return std::unexpected(resolved.error());
    }
    report = {
        .Progress = ModuleOperationProgress::kLoaded,
        .PrimaryError = std::nullopt,
        .Module = ModuleId(),
        .RelatedModule = ModuleId(),
    };
    return host;
}

pg::Status StaticModuleHost::Resolve(std::span<const OptionalDependencyBinding> optionalBindings,
                                     ModuleOperationReport& report) noexcept
{
    for (std::size_t index = 0; index < optionalBindings.size(); ++index)
    {
        const auto& binding = optionalBindings.subspan(index).front();
        const auto dependent = Find(binding.Dependent);
        if (!dependent ||
            std::ranges::find(optionalBindings.first(index), binding) != optionalBindings.first(index).end())
        {
            return Reject(report, ModuleError::kInvalidOptionalBinding, binding.Dependent, binding.Dependency);
        }
        const auto& dependencies = Records.at(*dependent).Descriptor.Dependencies;
        const auto found = std::ranges::find(dependencies, binding.Dependency, &ModuleDependency::Id);
        if (found == dependencies.end() || found->Kind != DependencyKind::kOptional)
        {
            return Reject(report, ModuleError::kInvalidOptionalBinding, binding.Dependent, binding.Dependency);
        }
    }
    for (auto& record : Records)
    {
        for (const auto& dependency : record.Descriptor.Dependencies)
        {
            const bool selected = std::ranges::find(optionalBindings, OptionalDependencyBinding{
                                                                          .Dependent = record.Descriptor.Id,
                                                                          .Dependency = dependency.Id,
                                                                      }) != optionalBindings.end();
            const bool required = dependency.Kind == DependencyKind::kRequired;
            const auto provider = Find(dependency.Id);
            auto reason = DependencyBindingReason::kNotSelected;
            if (!provider)
            {
                if (required || selected)
                {
                    return Reject(report, ModuleError::kMissingDependency, record.Descriptor.Id, dependency.Id);
                }
                reason = DependencyBindingReason::kMissing;
            }
            else if (Records.at(*provider).Descriptor.Version != dependency.Version)
            {
                if (required || selected)
                {
                    return Reject(report, ModuleError::kVersionMismatch, record.Descriptor.Id, dependency.Id);
                }
                reason = DependencyBindingReason::kVersionMismatch;
            }
            else if (required || selected)
            {
                reason = required ? DependencyBindingReason::kRequired : DependencyBindingReason::kSelected;
            }
            record.Bindings.push_back({.Dependency = dependency.Id, .Reason = reason});
        }
    }
    // Bounded deterministic topological ordering; ties follow the explicit Catalog order.
    Order.reserve(Records.size());
    while (Order.size() < Records.size())
    {
        const auto before = Order.size();
        for (const auto& record : Records)
        {
            if (std::ranges::find(Order, record.Descriptor.Id) != Order.end())
            {
                continue;
            }
            const bool ready = std::ranges::all_of(
                record.Bindings, [&](const auto& binding)
                { return !binding.IsBound() || std::ranges::find(Order, binding.Dependency) != Order.end(); });
            if (ready)
            {
                Order.push_back(record.Descriptor.Id);
            }
        }
        if (before == Order.size())
        {
            return Reject(report, ModuleError::kDependencyCycle);
        }
    }
    return {};
}

pg::Status StaticModuleHost::Register(ModuleId id, ModuleOperationReport& report) noexcept
{
    report = {};
    report.Module = id;
    if (Scope->Registering)
    {
        return Reject(report, ModuleError::kBusy, id);
    }
    const auto index = Find(id);
    if (!index)
    {
        return Reject(report, ModuleError::kNotFound, id);
    }
    auto& record = Records.at(*index);
    if (record.State != StaticModuleState::kLoaded)
    {
        return Reject(report, ModuleError::kInvalidState, id);
    }
    for (const auto& dependency : record.Bindings)
    {
        if (dependency.IsBound())
        {
            const auto provider = Find(dependency.Dependency);
            if (!provider || Records.at(*provider).State != StaticModuleState::kRegistered)
            {
                return Reject(report, ModuleError::kDependencyNotRegistered, id, dependency.Dependency);
            }
        }
    }

    struct ScopeGuard
    {
        bool* Registering;
        explicit ScopeGuard(bool& registering) noexcept
            : Registering(&registering)
        {
        }
        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;
        ScopeGuard(ScopeGuard&&) = delete;
        ScopeGuard& operator=(ScopeGuard&&) = delete;
        ~ScopeGuard() { *Registering = false; }
    };
    Scope->Registering = true;
    const ScopeGuard guard{Scope->Registering};
    RegistrationContext transaction(*Scope, id);
    const auto result = Callbacks.at(*index)(transaction);
    if (transaction.Failure || !result)
    {
        const auto error = transaction.Failure ? *transaction.Failure : result.error();
        record.State = StaticModuleState::kRegistrationFailed;
        report = {
            .Progress = ModuleOperationProgress::kRolledBack,
            .PrimaryError = error,
            .Module = id,
            .RelatedModule = ModuleId(),
        };
        return std::unexpected(error);
    }
    // No user code or recoverable operations occur during publication. Calls are serialized.
    Scope->Published.reserve(Scope->Published.size() + transaction.Pending.size());
    std::ranges::move(transaction.Pending, std::back_inserter(Scope->Published));
    record.State = StaticModuleState::kRegistered;
    report = {
        .Progress = ModuleOperationProgress::kRegistered,
        .PrimaryError = std::nullopt,
        .Module = id,
        .RelatedModule = ModuleId(),
    };
    return {};
}
} // namespace pg::module
