#pragma once

#include <ModuleDescriptor.h>
#include <ModuleIdentity.h>
#include <RegistrationContext.h>

#include <cstddef>
#include <vector>

namespace pg::module::fixture
{
struct FixtureValue
{
    std::vector<int> Values;
};
// Stable identities for these logical fixture Modules, independent of build/path.
inline constexpr ModuleId kFirstId{
    .Bytes =
        {
            std::byte{0xa1},
            std::byte{0x27},
            std::byte{0xf2},
            std::byte{0x19},
            std::byte{0x66},
            std::byte{0x4c},
            std::byte{0x44},
            std::byte{0x13},
            std::byte{0xb6},
            std::byte{0x70},
            std::byte{0xe8},
            std::byte{0x20},
            std::byte{0x62},
            std::byte{0x50},
            std::byte{0x88},
            std::byte{0xe1},
        },
};
inline constexpr ModuleId kSecondId{
    .Bytes =
        {
            std::byte{0x9d},
            std::byte{0x53},
            std::byte{0x48},
            std::byte{0xbb},
            std::byte{0xf2},
            std::byte{0x99},
            std::byte{0x43},
            std::byte{0x9f},
            std::byte{0x95},
            std::byte{0x14},
            std::byte{0xa1},
            std::byte{0x92},
            std::byte{0x91},
            std::byte{0xf0},
            std::byte{0x3a},
            std::byte{0x45},
        },
};
inline constexpr DeclarationId kFirstDeclaration{.Bytes = {std::byte{0xd1}}};
inline constexpr DeclarationId kSecondDeclaration{.Bytes = {std::byte{0xd2}}};

[[nodiscard]] const ModuleDefinition* QueryFirst() noexcept;
[[nodiscard]] const ModuleDefinition* QuerySecond() noexcept;
[[nodiscard]] StaticModuleCatalog GeneratedCatalog() noexcept;
} // namespace pg::module::fixture
