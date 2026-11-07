#pragma once
#include <ModuleDescriptor.h>
#include <PanguModuleExport.h>
namespace pg::module
{
inline constexpr ModuleId kModuleRuntimeModuleId{
    .Bytes =
        {
            std::byte{0xc0},
            std::byte{0xe3},
            std::byte{0x45},
            std::byte{0x7b},
            std::byte{0x71},
            std::byte{0xfb},
            std::byte{0x49},
            std::byte{0xe3},
            std::byte{0x9d},
            std::byte{0x0c},
            std::byte{0x8c},
            std::byte{0x0b},
            std::byte{0x23},
            std::byte{0x2e},
            std::byte{0xa4},
            std::byte{0x02},
        },
};
[[nodiscard]] PG_MODULE_API const ModuleDefinition* QueryModuleRuntime() noexcept;
[[nodiscard]] PG_MODULE_API std::span<const QueryModuleFn> BuiltinResidentQueries() noexcept;
} // namespace pg::module
