#pragma once

#include <HostServiceAccess.h>
#include <ModuleDescriptor.h>
#include <cstddef>

namespace pg::sample
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
            std::byte{3},
        },
};
inline constexpr module::DeclarationId kContextDeclaration{
    .Bytes =
        {
            std::byte{0xd4},
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

// POSIX 允许宿主与动态库按 RTTI 值匹配，Windows 不引入业务 DLL 类型导入。
#ifdef _WIN32
struct [[pga::Type]] CounterService
#else
struct [[pga::Type]] __attribute__((visibility("default"))) CounterService
#endif
{
    [[pga::Field]] int Value{};
};

[[nodiscard]] inline Result<module::ScopeAccess<CounterService>> AcquireCounter(const engine::EngineHost& host,
                                                                                module::ScopeHandle scope) noexcept
{
    return engine::detail::HostResourceAccess::Acquire<CounterService>(
        host,
        {.Scope = scope, .Owner = kModuleId, .Declaration = kContextDeclaration, .Kind = service::Scope::kContext});
}
[[nodiscard]] const module::ModuleDefinition* QueryMinimalServices() noexcept;
} // namespace pg::sample
