#pragma once

#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <PanguEngineHostExport.h>
#include <RegistrationContext.h>
#include <memory>
#include <vector>

namespace pg::engine
{
namespace detail
{
class HostResourceAccess;
struct EngineHostTestAccess;
} // namespace detail

// 管理操作和查询由调用方串行协调；构造不接纳或启动 Module。
class PG_ENGINE_HOST_API EngineHost final
{
public:
    EngineHost();
    // 兜底只执行一轮 Begin/Finish；操作中销毁或未能完成关闭均终止进程。
    ~EngineHost();
    EngineHost(const EngineHost&) = delete;
    EngineHost& operator=(const EngineHost&) = delete;
    EngineHost(EngineHost&&) = delete;
    EngineHost& operator=(EngineHost&&) = delete;

    [[nodiscard]] HostState GetState() const noexcept;
    [[nodiscard]] module::ScopeHandle HostScope() const noexcept;
    [[nodiscard]] Result<std::vector<module::ModuleSnapshot>> InspectModules() const noexcept;
    [[nodiscard]] Result<std::vector<module::RegisteredDeclaration>> InspectDeclarations() const noexcept;
    // 重入只返回 Busy，不修改 report；外层操作完成后一次性发布报告。
    [[nodiscard]] Status Start(const HostStartupConfig& config, HostOperationReport& report) noexcept;
    [[nodiscard]] Status BeginShutdown(HostOperationReport& report) noexcept;
    [[nodiscard]] Status TryFinishShutdown(HostOperationReport& report) noexcept;
    [[nodiscard]] Result<module::ScopeHandle> CreateContext(HostOperationReport& report) noexcept;
    [[nodiscard]] Result<module::ScopeHandle> CreateWorld(module::ScopeHandle context,
                                                          HostOperationReport& report) noexcept;
    [[nodiscard]] Status BeginCloseScope(module::ScopeHandle scope, HostOperationReport& report) noexcept;
    [[nodiscard]] Status TryFinishCloseScope(module::ScopeHandle scope, HostOperationReport& report) noexcept;
    [[nodiscard]] Status PollCleanup(HostOperationReport& report) noexcept;

private:
    friend class detail::HostResourceAccess;
    friend struct detail::EngineHostTestAccess;
    struct State;
    std::unique_ptr<State> Data;
};
} // namespace pg::engine
