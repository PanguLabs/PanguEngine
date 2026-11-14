#pragma once

#include "Driver/Diagnostic.h"
#include <filesystem>
#include <string>

namespace pg::meta::tool
{
struct ClangInfo
{
    std::string FrontendVersion;
    std::filesystem::path ResourceDir;
    bool HasRtti{};
    std::string HostStdlib;
};

[[nodiscard]] ToolResult<ClangInfo> CheckClang();
} // namespace pg::meta::tool
