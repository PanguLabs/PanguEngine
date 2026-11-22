#pragma once

#include <ModuleDeclaration.h>

#include <cstddef>

namespace pg::renderer::detail
{
inline constexpr module::DeclarationId kRendererSessionDeclaration{
    .Bytes =
        {
            std::byte{0xb3},
            std::byte{0x82},
            std::byte{0x0d},
            std::byte{0x76},
            std::byte{0x79},
            std::byte{0xda},
            std::byte{0x45},
            std::byte{0x97},
            std::byte{0x8d},
            std::byte{0xf1},
            std::byte{0x06},
            std::byte{0x8f},
            std::byte{0x64},
            std::byte{0x69},
            std::byte{0x59},
            std::byte{0xa1},
        },
};
} // namespace pg::renderer::detail
