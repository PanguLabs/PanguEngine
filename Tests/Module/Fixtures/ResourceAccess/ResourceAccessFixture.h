#pragma once

#include <ModuleDeclaration.h>
#include <ModuleDescriptor.h>

#include <cstddef>

namespace pg::module::test::resource_access
{
inline constexpr ModuleId kModuleId{
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
            std::byte{1},
        },
};
inline constexpr DeclarationId kHostDeclaration{
    .Bytes =
        {
            std::byte{0xd2},
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
inline constexpr DeclarationId kContextDeclaration{
    .Bytes =
        {
            std::byte{0xd2},
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
inline constexpr DeclarationId kWorldDeclaration{
    .Bytes =
        {
            std::byte{0xd2},
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
            std::byte{3},
        },
};
inline constexpr DeclarationId kSecondContextDeclaration{
    .Bytes =
        {
            std::byte{0xd2},
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
inline constexpr DeclarationId kMetadataDeclaration{
    .Bytes =
        {
            std::byte{0xd2},
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
            std::byte{5},
        },
};

#ifdef _WIN32
struct ScopeValue
#else
struct __attribute__((visibility("default"))) ScopeValue
#endif
{
    int Value{};
};
} // namespace pg::module::test::resource_access

namespace pg::module::test
{
[[nodiscard]] const ModuleDefinition* QueryResourceAccessFixture() noexcept;
}
