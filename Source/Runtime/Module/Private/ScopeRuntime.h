#pragma once

#include <ModuleIdentity.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeIdentity.h>
#include <StaticModuleHost.h>

#include <cstdint>
#include <memory>
#include <span>

namespace pg::module::detail
{
[[nodiscard]] pg::Result<std::uint64_t> AllocateScopeDomain() noexcept;

class ScopeRuntime final
{
public:
    ScopeRuntime(std::uint64_t domain, RegistrationScope& declarations, std::span<StaticModuleRecord> modules);
    ~ScopeRuntime();
    ScopeRuntime(const ScopeRuntime&) = delete;
    ScopeRuntime& operator=(const ScopeRuntime&) = delete;
    ScopeRuntime(ScopeRuntime&&) = delete;
    ScopeRuntime& operator=(ScopeRuntime&&) = delete;

    [[nodiscard]] bool TryBeginOperation() noexcept;
    void EndOperation() noexcept;
    [[nodiscard]] bool IsShuttingDown() const noexcept;
    void RegistrationFailed(ModuleId id, pg::Error error) noexcept;
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
    struct State;
    std::unique_ptr<State> Data;
};
} // namespace pg::module::detail
