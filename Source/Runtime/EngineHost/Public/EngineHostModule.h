#pragma once

#include <ModuleDescriptor.h>
#include <PanguEngineHostExport.h>

namespace pg::engine
{
inline constexpr module::ModuleId kEngineHostModuleId{
    .Bytes =
        {
            std::byte{0x8b},
            std::byte{0x07},
            std::byte{0xb8},
            std::byte{0xda},
            std::byte{0x6c},
            std::byte{0x96},
            std::byte{0x47},
            std::byte{0x46},
            std::byte{0x9f},
            std::byte{0x8a},
            std::byte{0x31},
            std::byte{0x60},
            std::byte{0x8f},
            std::byte{0x7e},
            std::byte{0x21},
            std::byte{0x93},
        },
};
[[nodiscard]] PG_ENGINE_HOST_API const module::ModuleDefinition* QueryEngineHostModule() noexcept;
} // namespace pg::engine
