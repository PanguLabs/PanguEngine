#pragma once

#include "CollectionRequest.h"
#include "ToolReport.h"
#include <Result.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pg::abi
{
enum class ResponseFileStyle : std::uint8_t
{
    kGnu,
    kWindows,
};

struct CompileUnit
{
    std::filesystem::path Compiler;
    std::filesystem::path Directory;
    std::filesystem::path Source;
    std::filesystem::path Object;
    std::vector<std::string> Arguments;
    std::vector<std::filesystem::path> ResponseFiles;
    std::vector<std::string> OriginalArguments;
    // Compiler/source/object/cwd have separate identities above. Only understood
    // compilation/dependency output controls are excluded; unknown options stay.
    std::vector<std::string> AbiArguments;
    // Also retain the normalized source token's position: e.g. -x changes
    // interpretation of subsequent inputs. This vector is for equality only.
    std::vector<std::string> ComparisonArguments;
    // Canonical Compiler is file identity only. argv[0] still selects a driver.
    std::string DriverName;
    std::string DriverMode;
    ResponseFileStyle ResponseStyle{};
};

[[nodiscard]] Result<std::vector<std::string>> ParseCommandLine(std::string_view text, CommandLineStyle style,
                                                                FailureReport& report);
[[nodiscard]] Result<std::vector<CompileUnit>> LoadCompileUnits(const CollectionRequest& request,
                                                                FailureReport& report);
[[nodiscard]] Result<CompileUnit> MatchCompilerInvocation(const CollectionRequest& request,
                                                          std::span<const std::string> arguments,
                                                          const std::filesystem::path& directory,
                                                          FailureReport& report);
} // namespace pg::abi
