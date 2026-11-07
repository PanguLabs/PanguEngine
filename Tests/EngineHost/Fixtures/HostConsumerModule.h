#pragma once
#include <ModuleDeclaration.h>
#include <ModuleDescriptor.h>
#include <cstddef>

namespace pg::engine::test
{
namespace consumer
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
            std::byte{4},
        },
};
inline constexpr module::DeclarationId kHostDeclaration{
    .Bytes =
        {
            std::byte{0xd5},
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
inline constexpr module::DeclarationId kWorldDeclaration{
    .Bytes =
        {
            std::byte{0xd5},
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
} // namespace consumer
[[nodiscard]] const module::ModuleDefinition* QueryHostConsumerFixture() noexcept;
} // namespace pg::engine::test
