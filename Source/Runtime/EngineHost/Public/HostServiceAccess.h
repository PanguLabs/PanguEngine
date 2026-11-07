#pragma once

#include <EngineHost.h>
#include <ScopeResourceAccess.h>
#include <type_traits>

namespace pg::engine::detail
{
// 业务具名入口固定自身身份；桥接只在本次查询期间借用 Manager。
class HostResourceAccess final
{
public:
    template <class T>
        requires(std::is_object_v<T> && !std::is_const_v<T> && !std::is_array_v<T> && std::is_nothrow_destructible_v<T>)
    [[nodiscard]] static Result<module::ScopeAccess<T>>
    Acquire(const EngineHost& host, const module::detail::ScopeResourceQuery& query) noexcept
    {
        const auto manager = CheckedManager(host);
        if (!manager)
        {
            return std::unexpected(manager.error());
        }
        return module::detail::ScopeResourceAccess::Acquire<T>(**manager, query);
    }

private:
    [[nodiscard]] PG_ENGINE_HOST_API static Result<const module::ModuleManager*>
    CheckedManager(const EngineHost& host) noexcept;
};
} // namespace pg::engine::detail
