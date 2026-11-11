#pragma once

#include <EngineHost.h>
#include <MetaTraits.h>
#include <ModuleIdentity.h>
#include <ModuleMetaBorrow.h>
#include <PanguEngineHostExport.h>
#include <Result.h>
#include <typeinfo>

namespace pg::engine::detail
{
class HostMetaAccess final
{
public:
    [[nodiscard]] PG_ENGINE_HOST_API static Result<module::ModuleMetaBorrow>
    Acquire(const EngineHost& host, module::ModuleId owner, const std::type_info& type) noexcept;
};
} // namespace pg::engine::detail

namespace pg::meta
{
template <class T>
    requires(TypeTraits<T>::HasDescription)
[[nodiscard]] Result<module::ModuleMetaBorrow> BorrowTypeInfo(const engine::EngineHost& host) noexcept
{
    return engine::detail::HostMetaAccess::Acquire(host, TypeTraits<T>::Owner, typeid(T));
}
} // namespace pg::meta
