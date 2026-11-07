#pragma once

#include <ModuleDeclaration.h>
#include <ModuleIdentity.h>
#include <ScopeAccess.h>
#include <ScopeIdentity.h>

#include <type_traits>
#include <typeinfo>

namespace pg::module
{
class ModuleManager;
namespace detail
{
struct ScopeResourceQuery
{
    ScopeHandle Scope{};
    ModuleId Owner{};
    DeclarationId Declaration{};
    pg::service::Scope Kind{pg::service::Scope::kHost};
};

class ScopeResourceAccess final
{
public:
    template <class T>
        requires(std::is_object_v<T> && !std::is_const_v<T> && !std::is_array_v<T> && std::is_nothrow_destructible_v<T>)
    [[nodiscard]] static pg::Result<ScopeAccess<T>> Acquire(const ModuleManager& manager,
                                                            const ScopeResourceQuery& query) noexcept
    {
        auto control = Resolve(manager, query, typeid(T));
        if (!control)
        {
            return std::unexpected(control.error());
        }
        return ScopeAccess<T>{std::move(*control)};
    }

private:
    [[nodiscard]] PG_MODULE_API static Result<std::shared_ptr<ScopeObjectControl>>
    Resolve(const ModuleManager& manager, const ScopeResourceQuery& query, const std::type_info& type) noexcept;
};
} // namespace detail
} // namespace pg::module
