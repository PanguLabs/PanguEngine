#pragma once

#include "PluginManifest.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pg::build::plugin
{
struct PluginEntry
{
    PluginManifest Metadata;
    std::filesystem::path SourceDir;
    std::filesystem::path ManifestPath;
    std::vector<std::filesystem::path> ConfigureInputs;
};

[[nodiscard]] std::expected<std::vector<PluginEntry>, std::string>
DiscoverPlugins(const std::filesystem::path& engineRoot,
                const std::optional<std::filesystem::path>& projectRoot) noexcept;
} // namespace pg::build::plugin
