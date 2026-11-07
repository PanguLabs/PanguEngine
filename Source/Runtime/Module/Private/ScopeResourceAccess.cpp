#include <ScopeResourceAccess.h>

#include <ModuleManager.h>
#include <Result.h>

#include <memory>
#include <typeinfo>

namespace pg::module::detail
{
Result<std::shared_ptr<ScopeObjectControl>> ScopeResourceAccess::Resolve(const ModuleManager& manager,
                                                                         const ScopeResourceQuery& query,
                                                                         const std::type_info& type) noexcept
{
    return manager.AcquireScopeResource(query, type);
}
} // namespace pg::module::detail
