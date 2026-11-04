#include "PluginPathInputs.h"
#include "PathEncoding.h"

#include <cstddef>
#include <deque>
#include <expected>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <Windows.h> // IWYU pragma: keep
#endif

namespace pg::build::plugin
{
namespace
{
namespace fs = std::filesystem;

[[nodiscard]] std::unexpected<std::string> Failure(const fs::path& path, std::string_view reason)
{
    return std::unexpected(PathToUtf8(path) + ": " + std::string(reason));
}

#ifdef _WIN32
// SDK 要求经 Windows.h 引入这些 API；include-cleaner 不识别该 umbrella 边界。
// NOLINTBEGIN(misc-include-cleaner)
[[nodiscard]] std::expected<bool, std::string> IsJunction(const fs::path& path)
{
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        return Failure(path, std::error_code(static_cast<int>(GetLastError()), std::system_category()).message());
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
    {
        return false;
    }
    WIN32_FIND_DATAW data{};
    auto* const handle = FindFirstFileW(path.c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return Failure(path, std::error_code(static_cast<int>(GetLastError()), std::system_category()).message());
    }
    FindClose(handle);
    return data.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT;
}
// NOLINTEND(misc-include-cleaner)

[[nodiscard]] fs::path NormalizeTarget(const fs::path& target)
{
    const auto& value = target.native();
    constexpr std::wstring_view kNtPrefix = LR"(\??\)";
    constexpr std::wstring_view kNtUncPrefix = LR"(\??\UNC\)";
    if (value.starts_with(kNtUncPrefix))
    {
        return {std::wstring(LR"(\\)") + value.substr(kNtUncPrefix.size())};
    }
    if (value.starts_with(kNtPrefix))
    {
        fs::path normalized(value.substr(kNtPrefix.size()));
        if (normalized.is_absolute())
        {
            return normalized;
        }
        return {std::wstring(LR"(\\?\)") + normalized.native()};
    }
    return target;
}
#endif
} // namespace

PluginPathInputs::PluginPathInputs(const std::filesystem::path& root, const std::filesystem::path& canonicalRoot)
{
    // 从首次可能存在别名的组件之前起步，保留工程根参数的链接祖先。
    auto resolved = canonicalRoot.begin();
    for (auto part = root.begin(); part != root.end() && resolved != canonicalRoot.end() && *part == *resolved;
         ++part, ++resolved)
    {
        Root /= *part;
    }
    if (!Root.is_absolute())
    {
        Root = root.root_path();
    }
}

std::expected<std::optional<std::filesystem::path>, std::string>
PluginPathInputs::LinkTarget(const std::filesystem::path& path)
{
    if (const auto found = Links.find(path); found != Links.end())
    {
        return found->second;
    }
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error)
    {
        return Failure(path, error.message());
    }
    bool link = std::filesystem::is_symlink(status);
#ifdef _WIN32
    if (!link && !std::filesystem::is_regular_file(status))
    {
        // MSVC STL 不把 Junction 标记为 file_type::symlink。
        const auto junction = IsJunction(path);
        if (!junction)
        {
            return std::unexpected(junction.error());
        }
        link = *junction;
    }
#endif
    std::optional<std::filesystem::path> target;
    if (link)
    {
        auto value = std::filesystem::read_symlink(path, error);
        if (error)
        {
            return Failure(path, error.message());
        }
#ifdef _WIN32
        value = NormalizeTarget(value);
#endif
        target = std::move(value);
    }
    Links.emplace(path, target);
    return target;
}

std::expected<void, std::string> PluginPathInputs::Track(const std::filesystem::path& file,
                                                         std::set<std::filesystem::path>& inputs)
{
    namespace fs = std::filesystem;
    auto resolved = Root;
    const auto relative = file.lexically_relative(Root);
    std::deque<fs::path> pending(relative.begin(), relative.end());
    std::size_t hops = 0;
    while (!pending.empty())
    {
        const auto part = std::move(pending.front());
        pending.pop_front();
        if (part.empty() || part == ".")
        {
            continue;
        }
        if (part == "..")
        {
            resolved = resolved.parent_path();
            continue;
        }
        resolved /= part;
        const auto target = LinkTarget(resolved);
        if (!target)
        {
            return std::unexpected(target.error());
        }
        if (!*target)
        {
            continue;
        }
        // 追踪每个链接自己的目录项；不观察目录目标，新增 Plugin 仍需显式 Configure。
        inputs.insert(resolved.parent_path());
        constexpr std::size_t kMaxLinkHops = 256;
        if (++hops > kMaxLinkHops)
        {
            return Failure(file, "too many symbolic links while collecting Configure inputs");
        }
        auto expanded = (*target)->is_absolute() ? **target : resolved.parent_path() / **target;
        if (!expanded.is_absolute())
        {
            std::error_code error;
            expanded = fs::absolute(expanded, error);
            if (error)
            {
                return Failure(file, error.message());
            }
        }
        auto suffix = expanded.lexically_relative(Root);
        if (!suffix.empty() && *suffix.begin() != "..")
        {
            resolved = Root;
        }
        else
        {
            resolved = expanded.root_path();
            suffix = expanded.relative_path();
        }
        for (auto next = suffix.end(); next != suffix.begin();)
        {
            pending.push_front(*--next);
        }
    }
    inputs.insert(resolved);
    return {};
}

std::expected<std::vector<std::filesystem::path>, std::string>
PluginPathInputs::Collect(std::span<const std::filesystem::path> files)
{
    std::set<std::filesystem::path> inputs;
    for (const auto& file : files)
    {
        if (const auto tracked = Track(file, inputs); !tracked)
        {
            return std::unexpected(tracked.error());
        }
    }
    return std::vector<std::filesystem::path>(inputs.begin(), inputs.end());
}
} // namespace pg::build::plugin
