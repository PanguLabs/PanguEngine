#pragma once

#include <Error.h>
#include <ModuleDescriptor.h>
#include <ModuleIdentity.h>
#include <Result.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pg::module
{
class RegistrationScope;

enum class DependencyBindingReason : std::uint8_t
{
    kRequired,
    kSelected,
    kMissing,
    kVersionMismatch,
    kNotSelected,
};

struct DependencyBinding
{
    ModuleId Dependency{};
    DependencyBindingReason Reason{};

    [[nodiscard]] constexpr bool IsBound() const noexcept
    {
        return Reason == DependencyBindingReason::kRequired || Reason == DependencyBindingReason::kSelected;
    }
};

struct OwnedModuleDescriptor
{
    ModuleId Id{};
    ModuleVersion Version{};
    std::uint32_t ProtocolVersion{1};
    std::vector<ModuleDependency> Dependencies;
    std::string DisplayName;
    std::string BuildIdentity;
};

enum class StaticModuleState : std::uint8_t
{
    kLoaded,
    kRegistered,
    kRegistrationFailed,
};

struct StaticModuleRecord
{
    OwnedModuleDescriptor Descriptor;
    std::vector<DependencyBinding> Bindings;
    StaticModuleState State{StaticModuleState::kLoaded};
};

enum class ModuleOperationProgress : std::uint8_t
{
    kUnchanged,
    kLoaded,
    kRegistered,
    kRolledBack,
};

// Fixed-size owning snapshot; no callback, string view or borrowed report lifetime.
struct ModuleOperationReport
{
    ModuleOperationProgress Progress{ModuleOperationProgress::kUnchanged};
    std::optional<pg::Error> PrimaryError;
    ModuleId Module{};
    ModuleId RelatedModule{};
};

// Same-binary STATIC startup only. No external loader, activation or unload API.
// Scope must outlive the Host. All calls, including callbacks, are externally serialized.
class StaticModuleHost final
{
public:
    ~StaticModuleHost() = default;
    StaticModuleHost(const StaticModuleHost&) = delete;
    StaticModuleHost& operator=(const StaticModuleHost&) = delete;
    StaticModuleHost(StaticModuleHost&&) noexcept = default;
    StaticModuleHost& operator=(StaticModuleHost&&) noexcept = default;

    [[nodiscard]] static pg::Result<StaticModuleHost>
    Create(const StaticModuleCatalog& catalog, RegistrationScope& scope,
           std::span<const OptionalDependencyBinding> optionalBindings, ModuleOperationReport& report) noexcept;

    // Views borrow this Host. Never move/destroy the Host during an operation/callback.
    [[nodiscard]] std::span<const StaticModuleRecord> Modules() const noexcept { return Records; }
    [[nodiscard]] std::span<const ModuleId> RegistrationOrder() const noexcept { return Order; }
    [[nodiscard]] pg::Status Register(ModuleId id, ModuleOperationReport& report) noexcept;

private:
    explicit StaticModuleHost(RegistrationScope& scope) noexcept;
    [[nodiscard]] std::optional<std::size_t> Find(ModuleId id) const noexcept;
    [[nodiscard]] pg::Status Resolve(std::span<const OptionalDependencyBinding> optionalBindings,
                                     ModuleOperationReport& report) noexcept;

    RegistrationScope* Scope;
    std::vector<StaticModuleRecord> Records;
    std::vector<RegisterModuleFn> Callbacks;
    std::vector<ModuleId> Order;
};
} // namespace pg::module
