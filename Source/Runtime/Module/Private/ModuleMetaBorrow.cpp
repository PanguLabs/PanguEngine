#include "MetaInfo.h"
#include "ModuleCodePin.h"
#include <ModuleMetaBorrow.h>
#include <utility>

namespace pg::module
{
ModuleMetaBorrow::ModuleMetaBorrow(ModuleCodePin pin, const meta::TypeInfo* info) noexcept
    : Pin(std::move(pin))
    , Info(info)
{
}
ModuleMetaBorrow::ModuleMetaBorrow(ModuleMetaBorrow&& other) noexcept
    : Pin(std::move(other.Pin))
    , Info(std::exchange(other.Info, nullptr))
{
}
ModuleMetaBorrow& ModuleMetaBorrow::operator=(ModuleMetaBorrow&& other) noexcept
{
    if (this != &other)
    {
        Release();
        Pin = std::move(other.Pin);
        Info = std::exchange(other.Info, nullptr);
    }
    return *this;
}
ModuleMetaBorrow::operator bool() const noexcept { return Info != nullptr; }
const meta::TypeInfo* ModuleMetaBorrow::Get() const& noexcept { return Info; }
void ModuleMetaBorrow::Release() noexcept
{
    Info = nullptr;
    Pin.Release();
}
} // namespace pg::module
