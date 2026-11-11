#pragma once
#include "Driver/GenerationRequest.h"
#include "Output/GeneratedFile.h"
namespace pg::meta::tool
{
[[nodiscard]] GeneratedFile EmitCatalog(const GenerationRequest& request);
}
