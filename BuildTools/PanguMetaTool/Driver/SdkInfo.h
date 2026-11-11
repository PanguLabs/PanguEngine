#pragma once

#include "Driver/Diagnostic.h"
#include <filesystem>
#include <string>

namespace pg::meta::tool
{
struct SdkInfo
{
    std::string FrontendVersion;
    std::filesystem::path ResourceDir;
    bool HasRtti{};
    std::string HostStdlib;
};

[[nodiscard]] ToolResult<SdkInfo> CheckSdk();
} // namespace pg::meta::tool
