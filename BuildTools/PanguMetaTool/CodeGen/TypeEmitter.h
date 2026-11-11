#pragma once
#include "Driver/GenerationRequest.h"
#include "MetaIR/Model.h"
#include "Output/GeneratedFile.h"
#include <span>
namespace pg::meta::tool
{
struct HeaderEmission
{
    std::filesystem::path Declaration;
    std::filesystem::path Implementation;
    std::string OriginalInclude;
    std::vector<const ir::TypeDefinition*> Types;
    bool TypedAccess{};
    bool Description{};
};
[[nodiscard]] std::vector<GeneratedFile> EmitTypes(const GenerationRequest& request, const HeaderEmission& header);
} // namespace pg::meta::tool
