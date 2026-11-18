#pragma once

#include <PanguModuleExport.h>
#include <Result.h>

namespace pg::module
{
class ModuleManager;
struct ModuleOperationReport;

namespace detail
{
// 由 EngineHost 在推进 Module 关闭前排空本宿主的 World。
class ModuleHostShutdownAccess final
{
public:
    [[nodiscard]] static PG_MODULE_API Result<bool> BeginWorldShutdown(ModuleManager& manager,
                                                                       ModuleOperationReport& destination) noexcept;
    [[nodiscard]] static PG_MODULE_API Status TryFinishWorldShutdown(ModuleManager& manager,
                                                                     ModuleOperationReport& destination) noexcept;
};
} // namespace detail
} // namespace pg::module
