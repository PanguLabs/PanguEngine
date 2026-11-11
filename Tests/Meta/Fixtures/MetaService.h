#pragma once
#include "MetaActor.h"
#include "MetaModule.h"
#include <HostServiceAccess.h>

namespace pg::test::meta_fixture
{
inline constexpr module::DeclarationId kActorDeclaration{
    .Bytes =
        {
            std::byte{0xe3},
            std::byte{},
            std::byte{},
            std::byte{},
            std::byte{},
            std::byte{},
            std::byte{},
            std::byte{},
            std::byte{},
            std::byte{},
            std::byte{},
            std::byte{},
            std::byte{},
            std::byte{},
            std::byte{},
            std::byte{1},
        },
};
#ifndef _WIN32
#pragma GCC visibility push(default)
#endif
struct [[pga::Type]] ActorService
{
    [[pga::Field]] Actor Value;
};
#ifndef _WIN32
#pragma GCC visibility pop
#endif
[[nodiscard]] inline Result<module::ScopeAccess<ActorService>> AcquireActor(const engine::EngineHost& host,
                                                                            module::ScopeHandle scope) noexcept
{
    return engine::detail::HostResourceAccess::Acquire<ActorService>(host, {
                                                                               .Scope = scope,
                                                                               .Owner = kServiceModuleId,
                                                                               .Declaration = kActorDeclaration,
                                                                               .Kind = service::Scope::kContext,
                                                                           });
}
} // namespace pg::test::meta_fixture
