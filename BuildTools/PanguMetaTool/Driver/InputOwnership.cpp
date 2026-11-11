#include "Driver/InputOwnership.h"
#include "Driver/Diagnostic.h"
#include "Driver/GenerationRequest.h"
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
namespace pg::meta::tool
{
namespace
{
using Path = std::filesystem::path;
std::unexpected<Diagnostics> Failure(std::string code, std::string message, const Path& path = {})
{
    const auto utf8 = path.generic_u8string();
    return std::unexpected(Diagnostics{
        {
            .Code = std::move(code),
            .Message = std::move(message),
            .Source = {.File = std::string(utf8.begin(), utf8.end()), .Line = 0, .Column = 0},
        },
    });
}

ToolResult<Path> Canonical(const Path& path)
{
    std::error_code error;
    auto result = std::filesystem::weakly_canonical(path, error);
    if (error)
    {
        return Failure("invalid_request", "无法规范化文件路径: " + std::to_string(error.value()), path);
    }
    return result;
}

ToolResult<bool> SamePath(const Path& left, const Path& right)
{
    const auto a = Canonical(left);
    if (!a)
    {
        return std::unexpected(a.error());
    }
    const auto b = Canonical(right);
    if (!b)
    {
        return std::unexpected(b.error());
    }
    if (*a == *b)
    {
        return true;
    }
    std::error_code error;
    // 未登记且尚不存在的输入应返回空归属；equivalent 只用于现存文件。
    const bool leftExists = std::filesystem::exists(*a, error);
    if (error)
    {
        return Failure("invalid_request", "无法检查文件身份: " + std::to_string(error.value()), left);
    }
    const bool rightExists = std::filesystem::exists(*b, error);
    if (error)
    {
        return Failure("invalid_request", "无法检查文件身份: " + std::to_string(error.value()), right);
    }
    if (!leftExists || !rightExists)
    {
        return false;
    }
    const bool equivalent = std::filesystem::equivalent(*a, *b, error);
    if (error && error != std::errc::no_such_file_or_directory)
    {
        return Failure("invalid_request", "无法比较文件身份: " + std::to_string(error.value()), left);
    }
    return equivalent;
}

} // namespace
ToolResult<InputOwnership> InputOwnership::Create(std::span<const OwnershipEntry> entries)
{
    std::vector<OwnershipEntry> owned;
    for (const auto& entry : entries)
    {
        if (!entry.Path.is_absolute() || entry.Target.empty())
        {
            return Failure("invalid_request", "归属登记必须包含绝对路径和目标", entry.Path);
        }
        for (const auto& previous : owned)
        {
            const auto same = SamePath(previous.Path, entry.Path);
            if (!same)
            {
                return std::unexpected(same.error());
            }
            if (*same)
            {
                return Failure("duplicate_owner", "同一输入文件被重复登记", entry.Path);
            }
        }
        owned.push_back(entry);
    }
    return InputOwnership(std::move(owned));
}

ToolResult<std::optional<std::string_view>> InputOwnership::Find(const Path& path) const
{
    for (const auto& entry : Entries)
    {
        const auto same = SamePath(entry.Path, path);
        if (!same)
        {
            return std::unexpected(same.error());
        }
        if (*same)
        {
            return std::optional<std::string_view>(entry.Target);
        }
    }
    return std::optional<std::string_view>{};
}
} // namespace pg::meta::tool
