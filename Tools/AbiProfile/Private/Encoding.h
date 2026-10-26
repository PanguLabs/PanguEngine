#pragma once

#include "ToolReport.h"

#include <filesystem>

namespace pg::abi
{
// Four-parameter encoding mode: 0 success, 2 arguments/schema/path,
// 3 invalid input Profile, 4 output/cleanup failure. Does not publish collection state.
[[nodiscard]] int EncodeProfileFiles(const std::filesystem::path& schemaPath, const std::filesystem::path& profilePath,
                                     const std::filesystem::path& canonicalOutput,
                                     const std::filesystem::path& fingerprintOutput, FailureReport& report);
} // namespace pg::abi
