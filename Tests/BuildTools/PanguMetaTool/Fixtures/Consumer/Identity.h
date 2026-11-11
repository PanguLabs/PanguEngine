#pragma once
#include <ModuleDescriptor.h>
#include <ModuleIdentity.h>
#include <cstddef>
namespace pg::test
{
inline constexpr module::ModuleId kActorModule = []
{
    module::ModuleId id{};
    id.Bytes.front() = std::byte{0xe9};
    id.Bytes.back() = std::byte{9};
    return id;
}();
[[nodiscard]] const module::ModuleDefinition* QueryActor() noexcept;
} // namespace pg::test
