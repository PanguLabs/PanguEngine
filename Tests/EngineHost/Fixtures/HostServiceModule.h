#pragma once

#include <HostServiceAccess.h>
#include <ModuleDeclaration.h>
#include <ModuleDescriptor.h>

#include <cstddef>

namespace pg::engine::test
{
inline constexpr module::ModuleId kModuleId{
    .Bytes =
        {
            std::byte{0xd1},
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
            std::byte{2},
        },
};
inline constexpr module::DeclarationId kHostDeclaration{
    .Bytes =
        {
            std::byte{0xd3},
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
inline constexpr module::DeclarationId kContextDeclaration{
    .Bytes =
        {
            std::byte{0xd3},
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
            std::byte{2},
        },
};
inline constexpr module::DeclarationId kWorldDeclaration{
    .Bytes =
        {
            std::byte{0xd3},
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
            std::byte{3},
        },
};
inline constexpr module::DeclarationId kSecondContextDeclaration{
    .Bytes =
        {
            std::byte{0xd3},
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
            std::byte{4},
        },
};
inline constexpr module::DeclarationId kMetadataDeclaration{
    .Bytes =
        {
            std::byte{0xd3},
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
            std::byte{5},
        },
};

#ifdef _WIN32
struct HostServiceValue
#else
struct __attribute__((visibility("default"))) HostServiceValue
#endif
{
    int Value{};
};
[[nodiscard]] inline Result<module::ScopeAccess<HostServiceValue>>
AcquireContextValue(const EngineHost& host, module::ScopeHandle scope) noexcept
{
    return detail::HostResourceAccess::Acquire<HostServiceValue>(
        host,
        {.Scope = scope, .Owner = kModuleId, .Declaration = kContextDeclaration, .Kind = service::Scope::kContext});
}
[[nodiscard]] inline Result<module::ScopeAccess<HostServiceValue>> AcquireWorldValue(const EngineHost& host,
                                                                                     module::ScopeHandle scope) noexcept
{
    return detail::HostResourceAccess::Acquire<HostServiceValue>(
        host, {.Scope = scope, .Owner = kModuleId, .Declaration = kWorldDeclaration, .Kind = service::Scope::kWorld});
}
} // namespace pg::engine::test

namespace pg::engine::test
{
[[nodiscard]] const module::ModuleDefinition* QueryHostServiceFixture() noexcept;
}
