#pragma once
#include <ModuleDescriptor.h>
#include <ModuleIdentity.h>
#include <OwnerExport.h>
#include <cstddef>
namespace fixture
{
inline constexpr pg::module::ModuleId kModuleId{.Bytes = {std::byte{0xe9}}};
[[nodiscard]] PG_META_OWNER_API const pg::module::ModuleDefinition* Query() noexcept;
} // namespace fixture
