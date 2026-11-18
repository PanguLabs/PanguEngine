#pragma once

#include <ModuleDescriptor.h>
#include <ModuleIdentity.h>
#include <PanguECSExport.h>
#include <cstddef>

namespace pg::ecs
{
inline constexpr module::ModuleId kEcsModuleId{
    .Bytes =
        {
            std::byte{0x76},
            std::byte{0xd9},
            std::byte{0x52},
            std::byte{0xcd},
            std::byte{0x18},
            std::byte{0x08},
            std::byte{0x4c},
            std::byte{0x8d},
            std::byte{0xa1},
            std::byte{0xdc},
            std::byte{0x38},
            std::byte{0xfa},
            std::byte{0x92},
            std::byte{0xa5},
            std::byte{0x84},
            std::byte{0xbf},
        },
};

[[nodiscard]] PG_ECS_API const module::ModuleDefinition* QueryEcsModule() noexcept;
} // namespace pg::ecs
