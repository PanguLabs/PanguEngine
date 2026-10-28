#include "ToolIo.h"
#include "ToolReport.h"
#include <Error.h>
#include <Result.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <ios>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <corecrt_wstdio.h>
#endif

namespace pg::abi
{
namespace
{
[[nodiscard]] std::error_code StdioError(int value) noexcept
{
    return value != 0 ? std::error_code{value, std::generic_category()} : std::make_error_code(std::io_errc::stream);
}

[[nodiscard]] Result<ToolFile> OpenFile(const std::filesystem::path& path, bool output, std::string_view stage,
                                        FailureReport& report)
{
    errno = 0;
    std::FILE* raw = nullptr;
#ifdef _WIN32
    const int openError = ::_wfopen_s(&raw, path.c_str(), output ? L"wb" : L"rb");
#else
    raw = std::fopen(path.c_str(), output ? "wb" : "rb");
    const int openError = errno;
#endif
    if (raw == nullptr)
    {
        const auto native = StdioError(openError);
        return std::unexpected(
            report.Add(output ? ToolError::kOpenOutput : ToolError::kReadInput, stage, path.generic_string(), native));
    }
    return ToolFile{raw};
}

[[nodiscard]] Status CloseFile(ToolFile& file, const std::filesystem::path& path, FailureReport& report)
{
    errno = 0;
    if (std::fclose(file.release()) != 0)
    {
        const auto native = StdioError(errno);
        return std::unexpected(report.Add(ToolError::kCloseFile, "close_file", path.generic_string(), native));
    }
    return {};
}
} // namespace

Result<ToolFile> OpenOutputFile(const std::filesystem::path& path, FailureReport& report)
{
    return OpenFile(path, true, "open_output", report);
}

Status CommitOutputFile(ToolFile& file, const std::filesystem::path& path, FailureReport& report)
{
    std::optional<Error> failure;
    errno = 0;
    if (std::fflush(file.get()) != 0)
    {
        const auto native = StdioError(errno);
        failure = report.Add(ToolError::kFlushOutput, "flush_output", path.generic_string(), native);
    }
    const auto closed = CloseFile(file, path, report);
    if (!closed && !failure)
    {
        failure = closed.error();
    }
    return failure ? Status{std::unexpected(*failure)} : Status{};
}

Result<std::string> ReadFile(const std::filesystem::path& path, std::string_view stage, FailureReport& report)
{
    auto opened = OpenFile(path, false, stage, report);
    if (!opened)
    {
        return std::unexpected(opened.error());
    }
    auto file = std::move(*opened);
    std::string contents;
    std::optional<Error> failure;
    std::array<char, 4096> buffer{};
    while (true)
    {
        errno = 0;
        const auto count = std::fread(buffer.data(), 1, buffer.size(), file.get());
        const int native = errno;
        contents.append(buffer.data(), count);
        if (count != buffer.size())
        {
            if (std::ferror(file.get()) != 0)
            {
                failure = report.Add(ToolError::kReadInput, stage, path.generic_string(), StdioError(native));
            }
            break;
        }
    }
    const auto closed = CloseFile(file, path, report);
    if (!closed && !failure)
    {
        failure = closed.error();
    }
    return failure ? Result<std::string>{std::unexpected(*failure)} : Result<std::string>{std::move(contents)};
}

Status WriteFile(const std::filesystem::path& path, std::string_view contents, FailureReport& report)
{
    auto opened = OpenOutputFile(path, report);
    if (!opened)
    {
        return std::unexpected(opened.error());
    }
    auto file = std::move(*opened);
    std::optional<Error> failure;
    errno = 0;
    if (std::fwrite(contents.data(), 1, contents.size(), file.get()) != contents.size())
    {
        const auto native = StdioError(errno);
        failure = report.Add(ToolError::kWriteOutput, "write_output", path.generic_string(), native);
    }
    const auto committed = CommitOutputFile(file, path, report);
    if (!committed && !failure)
    {
        failure = committed.error();
    }
    return failure ? Status{std::unexpected(*failure)} : Status{};
}

Status RemoveGeneratedFile(const std::filesystem::path& path, FailureReport& report)
{
    std::error_code error;
    std::filesystem::remove(path, error);
    if (error)
    {
        return std::unexpected(report.Add(ToolError::kRemoveOutput, "remove_output", path.generic_string(), error));
    }
    return {};
}

bool ClearGeneratedFiles(const std::filesystem::path& canonical, const std::filesystem::path& fingerprint,
                         FailureReport& report)
{
    const auto first = RemoveGeneratedFile(canonical, report);
    const auto second = RemoveGeneratedFile(fingerprint, report);
    const bool cleared = first.has_value() && second.has_value();
    report.CleanupIncomplete |= !cleared;
    return cleared;
}

Result<std::filesystem::path> ResolvePath(const std::filesystem::path& path, std::string_view stage,
                                          FailureReport& report)
{
    std::error_code error;
    auto candidate = std::filesystem::absolute(path, error);
    if (error || path.empty())
    {
        return std::unexpected(report.Add(ToolError::kResolvePath, stage, path.generic_string(),
                                          error ? error : std::make_error_code(std::errc::invalid_argument)));
    }
    std::vector<std::filesystem::path> suffix;
    for (;;)
    {
        // macOS system libc++ can return an empty weakly_canonical result with
        // error == 0 if its status/canonical calls race a removed lock or marker.
        // canonical reports ENOENT directly; peel only absent components and
        // keep symlink resolution and every other failure authoritative.
        auto resolved = std::filesystem::canonical(candidate, error);
        if (!error)
        {
            if (!resolved.is_absolute())
            {
                return std::unexpected(report.Add(ToolError::kResolvePath, stage, path.generic_string(),
                                                  std::make_error_code(std::errc::invalid_argument)));
            }
            for (const auto& component : std::views::reverse(suffix))
            {
                resolved /= component;
            }
            return resolved.lexically_normal();
        }
        const auto parent = candidate.parent_path();
        if (error != std::errc::no_such_file_or_directory || parent == candidate || parent.empty())
        {
            return std::unexpected(report.Add(ToolError::kResolvePath, stage, path.generic_string(), error));
        }
        suffix.push_back(candidate.filename());
        candidate = parent;
    }
}

Result<bool> IsSameFile(const std::filesystem::path& left, const std::filesystem::path& right, FailureReport& report)
{
    const auto resolvedLeft = ResolvePath(left, "resolve_path", report);
    if (!resolvedLeft)
    {
        return std::unexpected(resolvedLeft.error());
    }
    const auto resolvedRight = ResolvePath(right, "resolve_path", report);
    if (!resolvedRight)
    {
        return std::unexpected(resolvedRight.error());
    }
    if (*resolvedLeft == *resolvedRight)
    {
        return true;
    }
    // equivalent() may report operation_not_supported for absent files on some
    // filesystems. Only compare identities after confirming both files exist.
    std::error_code error;
    for (const auto* path : {&left, &right})
    {
        const bool exists = std::filesystem::exists(*path, error);
        if (error)
        {
            return std::unexpected(report.Add(ToolError::kResolvePath, "resolve_path", path->generic_string(), error));
        }
        if (!exists)
        {
            return false;
        }
    }
    const bool same = std::filesystem::equivalent(left, right, error);
    if (error)
    {
        return std::unexpected(report.Add(ToolError::kResolvePath, "resolve_path", left.generic_string(), error));
    }
    return !error && same;
}
} // namespace pg::abi
