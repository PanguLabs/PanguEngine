#pragma once

#include "JsonSchema.h"
#include "ToolReport.h"
#include <Result.h>

namespace pg::abi
{
// Private evidence-to-profile mapping. Evidence authenticity belongs to the
// controlled compiler/linker invocation; this function does not attest a binary.
[[nodiscard]] Result<Json> BuildWindowsProfile(const Json& evidence, FailureReport& report);
} // namespace pg::abi
