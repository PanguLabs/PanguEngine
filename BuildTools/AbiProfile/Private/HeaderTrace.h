#pragma once

#include "ToolReport.h"

#include <Result.h>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pg::abi
{
struct ClangHeader
{
    std::filesystem::path Spelling;
    std::filesystem::path Physical;
};

// Keep the logical -H spelling before resolving symlinks. Header identity and
// the include key are different facts and must not overwrite one another.
[[nodiscard]] Result<std::vector<ClangHeader>>
ReadClangHeaderTrace(std::string_view trace, const std::filesystem::path& compilerDirectory, FailureReport& report);

// Parse the physical -H paths emitted by the actual Clang invocation.
// Relative paths are relative to CompileUnit.Directory, NOT the collector
// process CWD. A reported but unreadable/missing file is a hard error.
[[nodiscard]] Result<std::vector<std::filesystem::path>>
ParseClangHeaderTrace(std::string_view trace, const std::filesystem::path& compilerDirectory, FailureReport& report);

// These checks belong to the collector's private compiler observation boundary.
// Compare presence as well as replacement text, including empty ABI defines.
[[nodiscard]] Status CheckLibcxxConfiguration(std::string_view actual, std::string_view baseline,
                                              FailureReport& report);
[[nodiscard]] Status CheckLibcxxDriverArguments(std::span<const std::string> arguments, FailureReport& report);
// Inspect actual files named by Source/-H; presumed #line locations cannot
// authorize source-local configuration changes. No extra compiler per header.
[[nodiscard]] Status CheckLibcxxSourceConfiguration(const std::filesystem::path& source,
                                                    std::span<const ClangHeader> observed,
                                                    const std::filesystem::path& headers,
                                                    const std::filesystem::path& site, FailureReport& report);

// Reject source-owned changes to MSVC STL/CRT ABI controls throughout each
// physical file, rather than trusting only their restored EOF definitions.
[[nodiscard]] Status CheckWindowsSourceConfiguration(const std::filesystem::path& source,
                                                     std::span<const ClangHeader> observed,
                                                     std::span<const std::filesystem::path> trustedRoots,
                                                     FailureReport& report);

// Every logical key must occur at its selected canonical path; a second origin
// for that key is rejected even when the installed copy is also present.
[[nodiscard]] Status CheckClangHeaderOrigins(std::span<const ClangHeader> headers, const std::filesystem::path& root,
                                             std::span<const std::string_view> keys, FailureReport& report);
} // namespace pg::abi
