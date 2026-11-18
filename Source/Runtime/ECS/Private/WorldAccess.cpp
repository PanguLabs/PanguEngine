#include <WorldAccess.h>

#include <EcsModule.h>
#include <EngineHost.h>
#include <HostServiceAccess.h>
#include <Result.h>
#include <ScopeAccess.h>
#include <ScopeIdentity.h>
#include <World.h>

namespace pg::ecs
{
Result<module::ScopeAccess<World>> AcquireWorld(const engine::EngineHost& host, module::ScopeHandle scope) noexcept
{
    return engine::detail::HostResourceAccess::Acquire<World>(
        host,
        {.Scope = scope, .Owner = kEcsModuleId, .Declaration = kEcsWorldDeclaration, .Kind = service::Scope::kWorld});
}
} // namespace pg::ecs
