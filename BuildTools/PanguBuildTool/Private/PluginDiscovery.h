#pragma once

#include "PluginManifest.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pg::build::plugin
{
enum class PluginOrigin : std::uint8_t
{
    kEngine,
    kProject,
};

struct PluginEntry
{
    PluginManifest Metadata;
    PluginOrigin Origin{PluginOrigin::kEngine};
    std::filesystem::path SourceDir;
    std::filesystem::path ManifestPath;
    std::vector<std::filesystem::path> ConfigureInputs;
};

[[nodiscard]] std::expected<std::vector<PluginEntry>, std::string>
DiscoverPlugins(const std::filesystem::path& engineRoot,
                const std::optional<std::filesystem::path>& projectRoot) noexcept;
} // namespace pg::build::plugin
