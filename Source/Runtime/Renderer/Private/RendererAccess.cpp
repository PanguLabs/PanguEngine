#include "RendererDeclaration.h"

#include <EngineHost.h>
#include <HostServiceAccess.h>
#include <RendererAccess.h>
#include <RendererModule.h>
#include <RendererSession.h>
#include <Result.h>
#include <ScopeAccess.h>
#include <ScopeIdentity.h>

namespace pg::renderer
{
Result<module::ScopeAccess<RendererSession>> AcquireRenderer(const engine::EngineHost& host) noexcept
{
    return engine::detail::HostResourceAccess::Acquire<RendererSession>(
        host, {
                  .Scope = host.HostScope(),
                  .Owner = kRendererModuleId,
                  .Declaration = detail::kRendererSessionDeclaration,
                  .Kind = service::Scope::kHost,
              });
}
} // namespace pg::renderer
