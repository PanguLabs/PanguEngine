#pragma once
#include "CodeGen/TypeEmitter.h"
namespace pg::meta::tool
{
[[nodiscard]] std::vector<GeneratedFile> EmitModule(const GenerationRequest& request,
                                                    std::span<const HeaderEmission> headers);
}
