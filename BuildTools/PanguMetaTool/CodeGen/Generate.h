#pragma once
#include "Driver/GenerationRequest.h"
#include "MetaIR/Model.h"
#include "Output/GeneratedFile.h"
namespace pg::meta::tool
{
[[nodiscard]] ToolResult<std::vector<GeneratedFile>> Generate(const GenerationRequest& request, const ir::Model& model);
}
