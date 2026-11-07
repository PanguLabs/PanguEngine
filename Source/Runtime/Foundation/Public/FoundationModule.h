#pragma once
#include <ModuleDescriptor.h>
#include <PanguFoundationExport.h>
namespace pg::module
{
inline constexpr ModuleId kFoundationModuleId{
    .Bytes =
        {
            std::byte{0x9f},
            std::byte{0x80},
            std::byte{0xb2},
            std::byte{0x58},
            std::byte{0x53},
            std::byte{0x20},
            std::byte{0x4d},
            std::byte{0xfb},
            std::byte{0xa4},
            std::byte{0x5a},
            std::byte{0xe9},
            std::byte{0xd8},
            std::byte{0x7c},
            std::byte{0x9a},
            std::byte{0x12},
            std::byte{0x01},
        },
};
[[nodiscard]] PG_FOUNDATION_API const ModuleDefinition* QueryFoundationModule() noexcept;
} // namespace pg::module
