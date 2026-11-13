#pragma once

#include "ModuleRecord.h"
#include <Error.h>
#include <ModuleIdentity.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeIdentity.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <typeinfo>

namespace pg::module::detail
{
struct ScopeResourceQuery;
struct ScopeObjectControl;
[[nodiscard]] pg::Result<std::uint64_t> AllocateScopeDomain() noexcept;

class ScopeRuntime final
{
public:
    ScopeRuntime(std::uint64_t domain, RegistrationScope& declarations, std::span<ModuleRecord* const> modules,
                 log::LogWriter writer = {});
    ~ScopeRuntime();
    ScopeRuntime(const ScopeRuntime&) = delete;
    ScopeRuntime& operator=(const ScopeRuntime&) = delete;
    ScopeRuntime(ScopeRuntime&&) = delete;
    ScopeRuntime& operator=(ScopeRuntime&&) = delete;

    [[nodiscard]] bool TryBeginOperation() noexcept;
    void EndOperation() noexcept;
    void Explain(ModuleOperationReport& report, ModuleId waitModule = {}, ScopeHandle waitScope = {}) const noexcept;
    [[nodiscard]] bool RememberFailure(ModuleHandle owner, ModuleId module, ScopeHandle scope,
                                       DeclarationId declaration, const FailureReport& failure,
                                       ModuleFailureOrigin origin) noexcept;
    [[nodiscard]] bool IsShuttingDown() const noexcept;
    [[nodiscard]] bool HasBusinessDependency(ModuleHandle handle) const noexcept;
    [[nodiscard]] std::optional<pg::Error> PendingFailure() const noexcept;
    void RegistrationFailed(ModuleId id, pg::Error error) noexcept;
    [[nodiscard]] ScopeHandle HostScope() const noexcept;
    [[nodiscard]] Result<std::shared_ptr<ScopeObjectControl>>
    AcquireScopeResource(const ScopeResourceQuery& query, const std::type_info& type) const noexcept;
    [[nodiscard]] pg::Result<ScopeHandle> CreateScope(pg::service::Scope kind, ScopeHandle parent,
                                                      ModuleOperationReport& report);
    [[nodiscard]] pg::Status Activate(ModuleId id, ModuleOperationReport& report);
    void AttachModules(std::span<ModuleRecord* const> records) noexcept;
    void DetachModule(ModuleHandle handle) noexcept;
    void RequestShutdown() noexcept;
    // Manager 的失败装载组尚持有未知依赖时，所有内部回滚也必须保留潜在 Provider。
    void SetCleanupBlocked(bool blocked) noexcept;
    [[nodiscard]] pg::Status BeginUnload(ModuleId id, ModuleOperationReport& report);
    [[nodiscard]] pg::Status TryFinishResources(ModuleHandle handle, ModuleOperationReport& report) noexcept;
    [[nodiscard]] pg::Status BeginCloseScope(ScopeHandle handle, ModuleOperationReport& report);
    [[nodiscard]] pg::Status TryFinishCloseScope(ScopeHandle handle, ModuleOperationReport& report);
    [[nodiscard]] pg::Status PollCleanup(ModuleOperationReport& report);
    [[nodiscard]] pg::Status BeginShutdown(ModuleOperationReport& report);
    [[nodiscard]] pg::Status TryFinishShutdown(ModuleOperationReport& report);

private:
    struct State;
    std::unique_ptr<State> Data;
};
} // namespace pg::module::detail
