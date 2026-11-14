#pragma once
#include <MetaOptions.h>
#include <ModuleDescriptor.h>
#include <PanguLayoutValueExport.h>

namespace layout
{
inline constexpr pg::module::ModuleId kModuleId{.Bytes = {std::byte{0xe4}}};
[[nodiscard]] PG_LAYOUT_VALUE_API const pg::module::ModuleDefinition* Query() noexcept;
inline constexpr const char* kProjectKey = PANGU_PROJECT_VALUE == 17 ? "A" : "B";
struct [[pga::Type]] Context
{
    [[pga::Field(pg::meta::FieldOptions{.Key = kProjectKey})]] int Value{};
};
} // namespace layout
