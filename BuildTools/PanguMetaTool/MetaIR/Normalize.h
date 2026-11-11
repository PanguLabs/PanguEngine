#pragma once

#include "Driver/Diagnostic.h"
#include "MetaIR/Model.h"
#include <string_view>

namespace pg::meta::ir
{
[[nodiscard]] tool::ToolResult<TypeId> ParseTypeId(std::string_view text);
[[nodiscard]] tool::ToolResult<Model> Normalize(Model model);
} // namespace pg::meta::ir
