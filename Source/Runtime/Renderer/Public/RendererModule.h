#pragma once

#include <ModuleDescriptor.h>
#include <ModuleIdentity.h>
#include <PanguRendererExport.h>

#include <cstddef>

namespace pg::renderer
{
inline constexpr module::ModuleId kRendererModuleId{
    .Bytes =
        {
            std::byte{0x82},
            std::byte{0xf1},
            std::byte{0x12},
            std::byte{0xda},
            std::byte{0xe1},
            std::byte{0x0a},
            std::byte{0x41},
            std::byte{0xc3},
            std::byte{0xa8},
            std::byte{0x16},
            std::byte{0x69},
            std::byte{0x46},
            std::byte{0x80},
            std::byte{0xbe},
            std::byte{0xde},
            std::byte{0x3a},
        },
};

[[nodiscard]] PG_RENDERER_API const module::ModuleDefinition* QueryRendererModule() noexcept;
} // namespace pg::renderer
