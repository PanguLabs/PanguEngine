#pragma once
#include "Driver/GenerationRequest.h"
#include "MetaIR/Model.h"
namespace pg::meta::tool
{
struct ScanResult
{
    ir::Model Model;
    std::vector<std::filesystem::path> Dependencies;
};
[[nodiscard]] ToolResult<ScanResult> Scan(const GenerationRequest& request);
void RegisterBuiltinAttributes();
} // namespace pg::meta::tool
