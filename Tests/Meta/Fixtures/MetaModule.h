#pragma once
#include <ModuleDescriptor.h>
#include <cstddef>

namespace pg::test::meta_fixture
{
inline constexpr module::ModuleId kModuleId{
    .Bytes =
        {
            std::byte{0xe1},
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
inline constexpr module::ModuleId kServiceModuleId{
    .Bytes =
        {
            std::byte{0xe1},
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
[[nodiscard]] const module::ModuleDefinition* QueryMetaModule() noexcept;
[[nodiscard]] const module::ModuleDefinition* QueryMetaService() noexcept;
} // namespace pg::test::meta_fixture
