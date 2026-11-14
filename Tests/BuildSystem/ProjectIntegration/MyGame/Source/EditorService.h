#pragma once
#include "FixtureModules.h"
#include <HostServiceAccess.h>
namespace pg::fixture
{
#ifdef _WIN32
struct EditorSession
#else
struct __attribute__((visibility("default"))) EditorSession
#endif
{
    int Value{11};
};
[[nodiscard]] inline Result<module::ScopeAccess<EditorSession>> AcquireEditorSession(const engine::EngineHost& host,
                                                                                     module::ScopeHandle scope) noexcept
{
    return engine::detail::HostResourceAccess::Acquire<EditorSession>(host, {
                                                                                .Scope = scope,
                                                                                .Owner = kEditorExtrasId,
                                                                                .Declaration = kEditorDeclaration,
                                                                                .Kind = service::Scope::kContext,
                                                                            });
}
} // namespace pg::fixture
