#include "EngineHostState.h"
#include <EngineHost.h>
#include <HostError.h>
#include <HostOperation.h>
#include <HostServiceAccess.h>
#include <ModuleManager.h>
#include <Result.h>
#include <expected>

namespace pg::engine::detail
{
Result<const module::ModuleManager*> HostResourceAccess::CheckedManager(const EngineHost& host) noexcept
{
    if (host.Data->Operating)
    {
        return std::unexpected(MakeHostError(HostError::kBusy));
    }
    if (host.Data->Value != HostState::kReady || !host.Data->Manager)
    {
        return std::unexpected(MakeHostError(HostError::kInvalidState));
    }
    return &*host.Data->Manager;
}
} // namespace pg::engine::detail
