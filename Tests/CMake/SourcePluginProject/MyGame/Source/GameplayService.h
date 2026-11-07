#pragma once
#include "FixtureModules.h"
#include <HostServiceAccess.h>
namespace pg::fixture
{
#ifdef _WIN32
struct GameplaySession
#else
struct __attribute__((visibility("default"))) GameplaySession
#endif
{
    int Value{7};
};
[[nodiscard]] inline Result<module::ScopeAccess<GameplaySession>>
AcquireGameplaySession(const engine::EngineHost& host, module::ScopeHandle scope) noexcept
{
    return engine::detail::HostResourceAccess::Acquire<GameplaySession>(
        host,
        {.Scope = scope, .Owner = kGameplayId, .Declaration = kGameplayDeclaration, .Kind = service::Scope::kContext});
}
} // namespace pg::fixture
