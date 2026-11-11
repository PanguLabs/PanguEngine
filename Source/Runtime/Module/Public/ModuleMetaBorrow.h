#pragma once

#include <MetaInfo.h>
#include <ModuleCodePin.h>
#include <PanguModuleExport.h>

namespace pg::module
{
// 视图随 Pin 存活；Release 仅撤销本次借用，不执行卸载。
class PG_MODULE_API ModuleMetaBorrow final
{
public:
    ModuleMetaBorrow() noexcept = default;
    ~ModuleMetaBorrow() noexcept = default;
    ModuleMetaBorrow(const ModuleMetaBorrow&) = delete;
    ModuleMetaBorrow& operator=(const ModuleMetaBorrow&) = delete;
    ModuleMetaBorrow(ModuleMetaBorrow&& other) noexcept;
    ModuleMetaBorrow& operator=(ModuleMetaBorrow&& other) noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] const meta::TypeInfo* Get() const& noexcept;
    void Release() noexcept;

private:
    friend class ModuleManager;
    ModuleMetaBorrow(ModuleCodePin pin, const meta::TypeInfo* info) noexcept;
    ModuleCodePin Pin;
    const meta::TypeInfo* Info{};
};
} // namespace pg::module
