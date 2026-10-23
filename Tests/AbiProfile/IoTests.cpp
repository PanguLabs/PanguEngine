#include "ToolIo.h"
#include "ToolReport.h"
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h> // IWYU pragma: keep - supplies SDK prerequisites for the native handle API.
#include <corecrt_io.h>
#include <handleapi.h>
// MSVC's nonstandard _fileno is declared directly by this CRT header.
#include <stdio.h> // NOLINT(modernize-deprecated-headers)
#include <winnt.h>
#else
#include <unistd.h>
#endif

namespace
{
[[nodiscard]] bool CheckLateCloseFailure(const std::filesystem::path& directory)
{
    pg::abi::FailureReport report;
    const auto path = directory / "AbiProfileIo-close.bin";
    auto file = pg::abi::OpenOutputFile(path, report);
    if (!file)
    {
        return false;
    }

    // Invalidate the OS handle while retaining the stdio object: flush can be
    // empty/successful, but its subsequent real close must fail and be reported.
#ifdef _WIN32
    const auto handle = ::_get_osfhandle(::_fileno(file->get()));
    // CRT exposes the native HANDLE as intptr_t; only the OS fault probe converts it back.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
    if (handle == -1 || ::CloseHandle(reinterpret_cast<HANDLE>(handle)) == 0)
    {
        return false;
    }
#else
    if (::close(::fileno(file->get())) != 0)
    {
        return false;
    }
#endif

    const auto committed = pg::abi::CommitOutputFile(*file, path, report);
    if (committed || *file || report.Count == 0)
    {
        return false;
    }
    bool sawCloseFailure = false;
    for (std::size_t i = 0; i < report.Count; ++i)
    {
        const auto& entry = report.Entries.at(i);
        sawCloseFailure |= entry.Value.Code() == static_cast<std::uint32_t>(pg::abi::ToolError::kCloseFile) &&
                           entry.Value.Native().has_value();
    }
    return sawCloseFailure;
}

[[nodiscard]] bool CheckReportOwnershipAndBudget()
{
    pg::abi::FailureReport report;
    std::string ownedPath = "original/profile.json";
    const auto primary = report.Add(pg::abi::ToolError::kReadInput, "read_profile", ownedPath,
                                    std::make_error_code(std::errc::permission_denied));
    ownedPath.assign("changed/after/collection.json");
    for (std::size_t i = 0; i < pg::abi::FailureReport::kCapacity + 1; ++i)
    {
        // Secondary cleanup errors cannot displace the primary error.
        (void)report.Add(pg::abi::ToolError::kRemoveOutput, "remove_output", "secondary.json");
    }
    if (report.Count != pg::abi::FailureReport::kCapacity || !report.Truncated ||
        report.Entries.front().Value != primary)
    {
        return false;
    }
    std::ostringstream output;
    report.WriteJson(output);
    const auto json = nlohmann::json::parse(output.str(), nullptr, false);
    return !json.is_discarded() && json.at("truncated") == true &&
           json.at("errors").at(0).at("path") == "original/profile.json" &&
           json.at("errors").at(0).at("native").at("code") ==
               std::make_error_code(std::errc::permission_denied).value();
}
} // namespace

int main(int argc, char* const* argv)
{
    if (argc != 2)
    {
        return 2;
    }
    // The argument count above guards this command-line access.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const bool passed = CheckLateCloseFailure(argv[1]) && CheckReportOwnershipAndBudget();
    if (!passed)
    {
        std::cerr << "ABI output close/report regression failed\n";
    }
    return passed ? 0 : 1;
}
