#pragma once
#include <ModuleCodePin.h>
#include <ModuleDescriptor.h>
#include <ModuleOperation.h>
#include <PanguModuleExport.h>
#include <Result.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <typeinfo>
#include <variant>
#include <vector>
namespace pg::module
{
class RegistrationScope;
namespace detail
{
struct ModuleManagerTestAccess;
class ScopeResourceAccess;
struct ScopeResourceQuery;
struct ScopeObjectControl;
} // namespace detail
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

enum class ModuleState : std::uint8_t
{
    kLoaded = 0,
    kRegistered = 1,
    kActive = 2,
    kCleanupPending = 3,
    kUnloaded = 4,
};

struct ModuleSnapshot
{
    OwnedModuleDescriptor Descriptor;
    std::vector<DependencyBinding> Bindings;
    ModuleState State{ModuleState::kLoaded};
    bool Resident{};
};

struct StaticModuleCandidate
{
    QueryModuleFn Query{};
};
struct SharedModuleCandidate
{
    ModuleId ExpectedId{};
    std::filesystem::path Path;
};
using ModuleLoadCandidate = std::variant<StaticModuleCandidate, SharedModuleCandidate>;
struct ModuleLoadResult
{
    std::vector<ModuleHandle> Modules;
};
// 管理操作与查询由调用方串行协调。移动不改变内部稳定状态的地址。
class PG_MODULE_API ModuleManager final
{
public:
    ~ModuleManager();
    ModuleManager(const ModuleManager&) = delete;
    ModuleManager& operator=(const ModuleManager&) = delete;
    ModuleManager(ModuleManager&& other) noexcept;
    ModuleManager& operator=(ModuleManager&& other) noexcept;
    [[nodiscard]] static Result<ModuleManager> Create(RegistrationScope& declarations,
                                                      std::span<const QueryModuleFn> queries,
                                                      std::span<const OptionalDependencyBinding> selections,
                                                      ModuleOperationReport& report) noexcept;
    [[nodiscard]] Result<ModuleLoadResult> Load(std::span<const ModuleLoadCandidate> candidates,
                                                std::span<const OptionalDependencyBinding> selections,
                                                ModuleOperationReport& report) noexcept;
    [[nodiscard]] Status Register(ModuleHandle handle, ModuleOperationReport& report) noexcept;
    [[nodiscard]] Status Activate(ModuleHandle handle, ModuleOperationReport& report) noexcept;
    [[nodiscard]] Status BeginUnload(ModuleHandle handle, ModuleOperationReport& report) noexcept;
    [[nodiscard]] Status TryFinishUnload(ModuleHandle handle, ModuleOperationReport& report) noexcept;
    [[nodiscard]] Result<ModuleCodePin> TryPinForBusiness(ModuleHandle handle) noexcept;
    [[nodiscard]] std::optional<ModuleHandle> Find(ModuleId id) const noexcept;
    [[nodiscard]] Result<ModuleSnapshot> Inspect(ModuleHandle handle) const noexcept;
    [[nodiscard]] std::vector<ModuleHandle> Modules() const;
    [[nodiscard]] std::vector<ModuleHandle> RegistrationOrder() const;
    [[nodiscard]] ScopeHandle HostScope() const noexcept;
    [[nodiscard]] Result<ScopeHandle> CreateScope(pg::service::Scope kind, ScopeHandle parent,
                                                  ModuleOperationReport& report) noexcept;
    [[nodiscard]] Status BeginCloseScope(ScopeHandle scope, ModuleOperationReport& report) noexcept;
    [[nodiscard]] Status TryFinishCloseScope(ScopeHandle scope, ModuleOperationReport& report) noexcept;
    [[nodiscard]] Status PollCleanup(ModuleOperationReport& report) noexcept;
    [[nodiscard]] Status BeginShutdown(ModuleOperationReport& report) noexcept;
    [[nodiscard]] Status TryFinishShutdown(ModuleOperationReport& report) noexcept;

private:
    friend struct detail::ModuleManagerTestAccess;
    friend class detail::ScopeResourceAccess;
    [[nodiscard]] Result<std::shared_ptr<detail::ScopeObjectControl>>
    AcquireScopeResource(const detail::ScopeResourceQuery& query, const std::type_info& type) const noexcept;
    struct State;
    explicit ModuleManager(std::unique_ptr<State> state) noexcept;
    [[nodiscard]] Status FinishRecord(ModuleHandle handle, ModuleOperationReport& report) noexcept;
    [[nodiscard]] Status Cleanup(ModuleOperationReport& report, bool confirmShutdown) noexcept;
    std::unique_ptr<State> Data;
};
} // namespace pg::module
