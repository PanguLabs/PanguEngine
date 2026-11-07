#pragma once

#include <PanguModuleExport.h>
#include <memory>

namespace pg::module
{
namespace detail
{
struct ModuleCodeControl;
}
class ModuleManager;

// 仅保护代码驻留；实例访问仍须取得对应 ScopeBorrow。
class PG_MODULE_API ModuleCodePin final
{
public:
    ModuleCodePin() noexcept;
    ~ModuleCodePin() noexcept;
    ModuleCodePin(const ModuleCodePin&) = delete;
    ModuleCodePin& operator=(const ModuleCodePin&) = delete;
    ModuleCodePin(ModuleCodePin&& other) noexcept;
    ModuleCodePin& operator=(ModuleCodePin&& other) noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;
    void Release() noexcept;

private:
    friend class ModuleManager;
    explicit ModuleCodePin(std::shared_ptr<detail::ModuleCodeControl> control) noexcept;
    std::shared_ptr<detail::ModuleCodeControl> Control;
};
} // namespace pg::module
