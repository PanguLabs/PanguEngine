#include "PluginManifest.h"
#include "PathEncoding.h"

#include <toml++/toml.h>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace pg::build::plugin
{
namespace
{
[[nodiscard]] constexpr bool IsLower(char value) noexcept { return value >= 'a' && value <= 'z'; }

[[nodiscard]] constexpr bool IsDigit(char value) noexcept { return value >= '0' && value <= '9'; }

[[nodiscard]] constexpr bool IsAlphaNumeric(char value) noexcept
{
    return IsLower(value) || (value >= 'A' && value <= 'Z') || IsDigit(value);
}

[[nodiscard]] bool IsValidId(std::string_view value) noexcept
{
    std::size_t offset = 0;
    while (offset < value.size())
    {
        if (!IsLower(value[offset++]))
        {
            return false;
        }
        while (offset < value.size() && (IsLower(value[offset]) || IsDigit(value[offset])))
        {
            ++offset;
        }
        while (offset < value.size() && value[offset] == '-')
        {
            ++offset;
            if (offset == value.size() || (!IsLower(value[offset]) && !IsDigit(value[offset])))
            {
                return false;
            }
            do
            {
                ++offset;
            } while (offset < value.size() && (IsLower(value[offset]) || IsDigit(value[offset])));
        }
        if (offset == value.size())
        {
            return true;
        }
        if (value[offset++] != '.' || offset == value.size())
        {
            return false;
        }
    }
    return false;
}

[[nodiscard]] bool ParseNumeric(std::string_view value, std::size_t& offset) noexcept
{
    const auto start = offset;
    while (offset < value.size() && IsDigit(value[offset]))
    {
        ++offset;
    }
    return offset > start && (offset - start == 1 || value[start] != '0');
}

[[nodiscard]] bool ParseIdentifiers(std::string_view value, std::size_t& offset, bool prerelease) noexcept
{
    while (offset < value.size())
    {
        const auto start = offset;
        bool allNumeric = true;
        while (offset < value.size() && value[offset] != '.' && value[offset] != '+')
        {
            const char current = value[offset];
            if (!IsAlphaNumeric(current) && current != '-')
            {
                return false;
            }
            allNumeric &= IsDigit(current);
            ++offset;
        }
        if (offset == start || (prerelease && allNumeric && offset - start > 1 && value[start] == '0'))
        {
            return false;
        }
        if (offset == value.size() || value[offset] == '+')
        {
            return true;
        }
        ++offset;
        if (offset == value.size())
        {
            return false;
        }
    }
    return false;
}

[[nodiscard]] bool IsValidSemVer(std::string_view value) noexcept
{
    std::size_t offset = 0;
    for (int component = 0; component < 3; ++component)
    {
        if (!ParseNumeric(value, offset))
        {
            return false;
        }
        if (component < 2 && (offset == value.size() || value[offset++] != '.'))
        {
            return false;
        }
    }
    if (offset < value.size() && value[offset] == '-')
    {
        ++offset;
        if (!ParseIdentifiers(value, offset, true))
        {
            return false;
        }
    }
    if (offset < value.size() && value[offset] == '+')
    {
        ++offset;
        if (!ParseIdentifiers(value, offset, false))
        {
            return false;
        }
    }
    return offset == value.size();
}

[[nodiscard]] std::unexpected<std::string> Failure(const std::filesystem::path& path, std::string_view reason)
{
    return std::unexpected(PathToUtf8(path) + ": " + std::string(reason));
}
} // namespace

std::expected<PluginManifest, std::string> ParsePluginManifest(const std::filesystem::path& manifestPath) noexcept
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(manifestPath, error))
    {
        return Failure(manifestPath, error ? error.message() : "Manifest is not a regular file");
    }

    toml::parse_result parsed = toml::parse_file(manifestPath.u8string());
    if (!parsed)
    {
        return Failure(manifestPath, parsed.error().description());
    }
    const toml::table& root = parsed.table();
    if (root.size() != 1)
    {
        return Failure(manifestPath, "Only the [plugin] section is supported");
    }
    const toml::node* pluginNode = root.get("plugin");
    const toml::table* plugin = pluginNode == nullptr ? nullptr : pluginNode->as_table();
    if (plugin == nullptr)
    {
        return Failure(manifestPath, "Missing [plugin] section");
    }
    for (const auto& [key, value] : *plugin)
    {
        const auto field = key.str();
        if (field != "id" && field != "name" && field != "version")
        {
            return Failure(manifestPath, "Unknown [plugin] field: " + std::string(field));
        }
    }
    const toml::node* idNode = plugin->get("id");
    const auto id = idNode == nullptr ? std::optional<std::string>{} : idNode->value<std::string>();
    if (!id || !IsValidId(*id))
    {
        return Failure(manifestPath, "[plugin].id must be a lowercase dotted identifier");
    }
    PluginManifest manifest{.Id = *id};
    if (const auto* name = plugin->get("name"))
    {
        const auto text = name->value<std::string>();
        if (!text)
        {
            return Failure(manifestPath, "[plugin].name must be a string");
        }
        manifest.Name = *text;
    }
    if (const auto* version = plugin->get("version"))
    {
        const auto text = version->value<std::string>();
        if (!text || !IsValidSemVer(*text))
        {
            return Failure(manifestPath, "[plugin].version must be SemVer 2.0.0");
        }
        manifest.Version = *text;
    }
    return manifest;
}
} // namespace pg::build::plugin
