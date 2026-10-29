#pragma once

#include <Error.h>
#include <ModuleDeclaration.h>
#include <ModuleDescriptor.h>
#include <ModuleIdentity.h>
#include <Result.h>
#include <ScopeIdentity.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pg::module
{
class RegistrationScope;
namespace detail
{
class ScopeRuntime;
}

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
    kLoaded = 0,
    kRegistered = 1,
    kActive = 2,
    kCleanupPending = 3,
    kUnloaded = 4,
    kRegistrationFailed = kCleanupPending, // Source compatibility for the first metadata-only slice.
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
    kActive,
    kCleanupPending,
    kUnloaded,
    kScopeReady,
    kScopeClosed,
};

// Fixed-size owning snapshot; no callback, string view or borrowed report lifetime.
struct ModuleOperationReport
{
    ModuleOperationProgress Progress{ModuleOperationProgress::kUnchanged};
    std::optional<pg::Error> PrimaryError;
    ModuleId Module{};
    ModuleId RelatedModule{};
    ScopeHandle Scope{};
    DeclarationId Declaration{};
    std::optional<pg::Error> OriginalError = std::nullopt;
    std::optional<pg::Error> CleanupError = std::nullopt;
};

// Same-binary STATIC coordinator; logical lifecycle does not perform OS unload.
// Declaration domain outlives Host. Management calls are externally serialized;
// exported ScopeAccess/ScopeBorrow support concurrent admission and close.
class StaticModuleHost final
{
public:
    ~StaticModuleHost();
    StaticModuleHost(const StaticModuleHost&) = delete;
    StaticModuleHost& operator=(const StaticModuleHost&) = delete;
    StaticModuleHost(StaticModuleHost&& /*other*/) noexcept;
    StaticModuleHost& operator=(StaticModuleHost&& /*other*/) noexcept;

    [[nodiscard]] static pg::Result<StaticModuleHost>
    Create(const StaticModuleCatalog& catalog, RegistrationScope& scope,
           std::span<const OptionalDependencyBinding> optionalBindings, ModuleOperationReport& report) noexcept;

    // Views borrow this Host. Never move/destroy the Host during an operation/callback.
    [[nodiscard]] std::span<const StaticModuleRecord> Modules() const noexcept { return Records; }
    [[nodiscard]] std::span<const ModuleId> RegistrationOrder() const noexcept { return Order; }
    [[nodiscard]] pg::Status Register(ModuleId id, ModuleOperationReport& report) noexcept;
    [[nodiscard]] ScopeHandle HostScope() const noexcept;
    [[nodiscard]] pg::Result<ScopeHandle> CreateScope(pg::service::Scope kind, ScopeHandle parent,
                                                      ModuleOperationReport& report);
    [[nodiscard]] pg::Status Activate(ModuleId id, ModuleOperationReport& report);
    [[nodiscard]] pg::Status Load(ModuleId id, ModuleOperationReport& report);
    [[nodiscard]] pg::Status BeginUnload(ModuleId id, ModuleOperationReport& report);
    [[nodiscard]] pg::Status TryFinishUnload(ModuleId id, ModuleOperationReport& report);
    [[nodiscard]] pg::Status BeginCloseScope(ScopeHandle scope, ModuleOperationReport& report);
    [[nodiscard]] pg::Status TryFinishCloseScope(ScopeHandle scope, ModuleOperationReport& report);
    [[nodiscard]] pg::Status PollCleanup(ModuleOperationReport& report);
    [[nodiscard]] pg::Status BeginShutdown(ModuleOperationReport& report);
    [[nodiscard]] pg::Status TryFinishShutdown(ModuleOperationReport& report);

private:
    explicit StaticModuleHost(RegistrationScope& scope) noexcept;
    [[nodiscard]] std::optional<std::size_t> Find(ModuleId id) const noexcept;
    [[nodiscard]] pg::Status Resolve(std::span<const OptionalDependencyBinding> optionalBindings,
                                     ModuleOperationReport& report) noexcept;

    RegistrationScope* Scope;
    std::vector<StaticModuleRecord> Records;
    std::vector<RegisterModuleFn> Callbacks;
    std::vector<ModuleId> Order;
    std::uint64_t Domain{};
    std::unique_ptr<detail::ScopeRuntime> Runtime;
};
} // namespace pg::module
