#pragma once

#include "ToolReport.h"
#include <Result.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace pg::abi
{
struct FileCloser
{
    void operator()(std::FILE* file) const noexcept
    {
        // Normal paths close explicitly and report errors; this is a release fallback.
        // unique_ptr supplies its owned FILE to this deleter; no raw ownership escapes.
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        (void)std::fclose(file);
    }
};
using ToolFile = std::unique_ptr<std::FILE, FileCloser>;

[[nodiscard]] Result<ToolFile> OpenOutputFile(const std::filesystem::path& path, FailureReport& report);
[[nodiscard]] Status CommitOutputFile(ToolFile& file, const std::filesystem::path& path, FailureReport& report);
[[nodiscard]] Result<std::string> ReadFile(const std::filesystem::path& path, std::string_view stage,
                                           FailureReport& report);
[[nodiscard]] Status WriteFile(const std::filesystem::path& path, std::string_view contents, FailureReport& report);
[[nodiscard]] Status RemoveGeneratedFile(const std::filesystem::path& path, FailureReport& report);
[[nodiscard]] bool ClearGeneratedFiles(const std::filesystem::path& canonical, const std::filesystem::path& fingerprint,
                                       FailureReport& report);
[[nodiscard]] Result<bool> IsSameFile(const std::filesystem::path& left, const std::filesystem::path& right,
                                      FailureReport& report);
} // namespace pg::abi
