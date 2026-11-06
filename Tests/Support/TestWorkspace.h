#pragma once

#include <algorithm>
#include <expected>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace pg::test
{
[[nodiscard]] inline std::expected<std::filesystem::path, std::error_code>
CreateTestWorkspace(const std::filesystem::path& root, std::string_view caseName)
{
    const std::filesystem::path relative(caseName);
    if (relative.empty() || relative.has_root_path())
    {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    for (const auto& component : relative)
    {
        if (component == "..")
        {
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        }
    }
    std::error_code error;
    const auto canonicalRoot = std::filesystem::weakly_canonical(root, error);
    if (error)
    {
        return std::unexpected(error);
    }
    auto candidate = std::filesystem::weakly_canonical(canonicalRoot / relative, error);
    if (error)
    {
        return std::unexpected(error);
    }
    // 逐路径段比较，不能用字符串前缀放行同名前缀目录或符号链接逃逸。
    const auto [rootEnd, candidateEnd] = std::ranges::mismatch(canonicalRoot, candidate);
    if (rootEnd != canonicalRoot.end() || candidateEnd == candidate.end())
    {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    std::filesystem::create_directories(candidate, error);
    if (error)
    {
        return std::unexpected(error);
    }
    return candidate;
}
} // namespace pg::test
