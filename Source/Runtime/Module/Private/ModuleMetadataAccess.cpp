#include "ModuleIdentity.h"
#include "ModuleMetaBorrow.h"
#include "Result.h"
#include <ModuleManager.h>
#include <ModuleMetadataAccess.h>
#include <typeinfo>

namespace pg::module::detail
{
Result<ModuleMetaBorrow> ModuleMetadataAccess::Acquire(const ModuleManager& manager, ModuleId owner,
                                                       const std::type_info& type) noexcept
{
    return manager.AcquireMetadata(owner, type);
}
} // namespace pg::module::detail
