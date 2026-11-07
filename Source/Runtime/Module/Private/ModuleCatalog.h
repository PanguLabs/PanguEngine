#pragma once
#include "ModuleRecord.h"
namespace pg::module::detail
{
[[nodiscard]] Status CopyDefinition(ModuleRecord& record, QueryModuleFn query, ModuleOperationReport& report) noexcept;
[[nodiscard]] Result<std::vector<ModuleRecord*>> ResolveCatalog(std::span<ModuleRecord* const> existing,
                                                                std::span<ModuleRecord* const> added,
                                                                std::span<const OptionalDependencyBinding> selections,
                                                                ModuleOperationReport& report) noexcept;
} // namespace pg::module::detail
