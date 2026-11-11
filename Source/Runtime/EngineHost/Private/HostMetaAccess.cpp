#include "EngineHost.h"
#include "EngineHostState.h"
#include "HostError.h"
#include "HostOperation.h"
#include "ModuleIdentity.h"
#include "ModuleMetaBorrow.h"
#include "Result.h"
#include <HostMetaAccess.h>
#include <ModuleMetadataAccess.h>
#include <expected>
#include <typeinfo>

namespace pg::engine::detail
{
Result<module::ModuleMetaBorrow> HostMetaAccess::Acquire(const EngineHost& host, module::ModuleId owner,
                                                         const std::type_info& type) noexcept
{
    if (host.Data->Operating)
    {
        return std::unexpected(MakeHostError(HostError::kBusy));
    }
    if (host.Data->Value != HostState::kReady || !host.Data->Manager)
    {
        return std::unexpected(MakeHostError(HostError::kInvalidState));
    }
    return module::detail::ModuleMetadataAccess::Acquire(*host.Data->Manager, owner, type);
}
} // namespace pg::engine::detail
