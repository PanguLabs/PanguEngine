#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace pg::build::plugin
{
struct PluginManifest
{
    std::string Id;
    std::string Name;
    std::string Version;
};

[[nodiscard]] std::expected<PluginManifest, std::string>
ParsePluginManifest(const std::filesystem::path& manifestPath) noexcept;
} // namespace pg::build::plugin
