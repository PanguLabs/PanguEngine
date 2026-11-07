#include "ModuleCodeControl.h"
#include <ModuleCodePin.h>

#include <atomic>
#include <memory>
#include <utility>

namespace pg::module
{
namespace detail
{
std::shared_ptr<ModuleCodeControl> MakeModuleCodeControl() noexcept { return std::make_shared<ModuleCodeControl>(); }
} // namespace detail

ModuleCodePin::ModuleCodePin() noexcept = default;
ModuleCodePin::ModuleCodePin(std::shared_ptr<detail::ModuleCodeControl> control) noexcept
    : Control(std::move(control))
{
}
ModuleCodePin::~ModuleCodePin() noexcept { Release(); }
ModuleCodePin::ModuleCodePin(ModuleCodePin&&) noexcept = default;
ModuleCodePin& ModuleCodePin::operator=(ModuleCodePin&& other) noexcept
{
    if (this != &other)
    {
        Release();
        Control = std::move(other.Control);
    }
    return *this;
}
ModuleCodePin::operator bool() const noexcept { return Control != nullptr; }
void ModuleCodePin::Release() noexcept
{
    if (Control)
    {
        Control->ExternalPins.fetch_sub(1, std::memory_order_release);
        Control.reset();
    }
}
} // namespace pg::module
