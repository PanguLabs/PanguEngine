#pragma once

#include <ModuleDescriptor.h>
#include <PanguLogExport.h>

namespace pg::log
{
inline constexpr module::ModuleId kLogModuleId{
    .Bytes =
        {
            std::byte{0xb6},
            std::byte{0xdd},
            std::byte{0xc3},
            std::byte{0xfd},
            std::byte{0xc5},
            std::byte{0xb3},
            std::byte{0x4a},
            std::byte{0x2e},
            std::byte{0x90},
            std::byte{0x8c},
            std::byte{0x56},
            std::byte{0x6d},
            std::byte{0x90},
            std::byte{0xdb},
            std::byte{0x1e},
            std::byte{0x21},
        },
};
[[nodiscard]] PG_LOG_API const module::ModuleDefinition* QueryLogModule() noexcept;
} // namespace pg::log
