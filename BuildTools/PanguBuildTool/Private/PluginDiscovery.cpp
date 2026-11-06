#include "PluginDiscovery.h"
#include "PathEncoding.h"
#include "PluginPathInputs.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace pg::build::plugin
{
namespace
{
namespace fs = std::filesystem;

[[nodiscard]] std::unexpected<std::string> Error(const fs::path& source, std::string_view reason)
{
    return std::unexpected(PathToUtf8(source) + ": " + std::string(reason));
}

[[nodiscard]] bool IsWithin(const fs::path& child, const fs::path& root) noexcept
{
    auto part = child.begin();
    for (const auto& component : root)
    {
        if (part == child.end() || component != *part)
        {
            return false;
        }
        ++part;
    }
    return part != child.end();
}

[[nodiscard]] bool IsDirectChild(const fs::path& child, const fs::path& root) noexcept
{
    if (!IsWithin(child, root))
    {
        return false;
    }
    auto part = child.begin();
    std::advance(part, static_cast<std::ptrdiff_t>(std::distance(root.begin(), root.end())));
    return std::next(part) == child.end();
}

[[nodiscard]] std::string LowerAscii(std::string text)
{
    for (char& value : text)
    {
        if (value >= 'A' && value <= 'Z')
        {
            value = static_cast<char>(value - 'A' + 'a');
        }
    }
    return text;
}

[[nodiscard]] std::expected<fs::path, std::string> Canonical(const fs::path& path)
{
    std::error_code error;
    auto normalized = fs::canonical(path, error);
    if (error)
    {
        return Error(path, error.message());
    }
    return normalized;
}

[[nodiscard]] std::expected<fs::path, std::string> CheckRoot(const fs::path& root)
{
    std::error_code error;
    if (!fs::is_directory(root, error))
    {
        return Error(root, error ? error.message() : "discovery root is not a directory");
    }
    return Canonical(root);
}

[[nodiscard]] std::expected<fs::path, std::string> CheckFile(const fs::path& file, const fs::path& directory)
{
    std::error_code error;
    if (!fs::is_regular_file(file, error))
    {
        return Error(file, error ? error.message() : "plugin requires a regular file");
    }
    auto real = Canonical(file);
    if (!real)
    {
        return std::unexpected(real.error());
    }
    if (!IsWithin(*real, directory))
    {
        return Error(file, "plugin file escapes source directory");
    }
    return real;
}

[[nodiscard]] std::expected<void, std::string> ScanRoot(const fs::path& root, const fs::path& canonicalRoot,
                                                        PluginOrigin origin, std::vector<PluginEntry>& output,
                                                        std::map<std::string, fs::path>& ids,
                                                        std::set<fs::path>& visited)
{
    const fs::path plugins = root / "Plugins";
    std::error_code error;
    if (!fs::exists(plugins, error) && !error)
    {
        return {};
    }
    if (error)
    {
        return Error(plugins, error.message());
    }
    if (!fs::is_directory(plugins, error))
    {
        return Error(plugins, error ? error.message() : "Plugins must be a directory");
    }
    auto realRoot = Canonical(plugins);
    if (!realRoot)
    {
        return std::unexpected(realRoot.error());
    }
    if (!IsWithin(*realRoot, canonicalRoot))
    {
        return Error(plugins, "Plugins directory escapes discovery root");
    }

    std::map<std::string, fs::path> directoryNames;
    PluginPathInputs pathInputs(root, canonicalRoot);
    fs::directory_iterator it(plugins, error);
    if (error)
    {
        return Error(plugins, error.message());
    }
    const fs::directory_iterator end;
    for (; it != end; it.increment(error))
    {
        if (error)
        {
            return Error(plugins, error.message());
        }

        const fs::path candidate = it->path();
        if (!it->is_directory(error))
        {
            if (error)
            {
                return Error(candidate, error.message());
            }
            continue;
        }

        const fs::path manifest = candidate / "Plugin.toml";
        const fs::path cmake = candidate / "CMakeLists.txt";
        const bool hasManifest = fs::exists(manifest, error);
        if (error)
        {
            return Error(manifest, error.message());
        }
        const bool hasCMake = fs::exists(cmake, error);
        if (error)
        {
            return Error(cmake, error.message());
        }
        if (!hasManifest && !hasCMake)
        {
            continue;
        }
        if (!hasManifest || !hasCMake)
        {
            return Error(candidate, "plugin requires both Plugin.toml and CMakeLists.txt");
        }

        auto realDirectory = Canonical(candidate);
        if (!realDirectory)
        {
            return std::unexpected(realDirectory.error());
        }
        if (!IsDirectChild(*realDirectory, *realRoot))
        {
            return Error(candidate, "plugin directory is not a direct child or escapes Plugins root");
        }
        if (!visited.insert(*realDirectory).second)
        {
            return Error(candidate, "duplicate canonical plugin directory");
        }
        auto lowerName = LowerAscii(PathToUtf8(candidate.filename()));
        if (auto [where, inserted] = directoryNames.emplace(lowerName, candidate); !inserted)
        {
            return Error(candidate, "case-insensitive plugin directory collision: " + PathToUtf8(where->second));
        }
        auto realManifest = CheckFile(manifest, *realDirectory);
        if (!realManifest)
        {
            return std::unexpected(realManifest.error());
        }
        if (auto checkedCmake = CheckFile(cmake, *realDirectory); !checkedCmake)
        {
            return std::unexpected(checkedCmake.error());
        }
        auto metadata = ParsePluginManifest(manifest);
        if (!metadata)
        {
            return std::unexpected(metadata.error());
        }
        if (auto [where, inserted] = ids.emplace(metadata->Id, manifest); !inserted)
        {
            return Error(manifest, "duplicate plugin id '" + metadata->Id + "' also in " + PathToUtf8(where->second));
        }
        const std::array requiredFiles{manifest, cmake};
        auto configureInputs = pathInputs.Collect(requiredFiles);
        if (!configureInputs)
        {
            return std::unexpected(configureInputs.error());
        }
        output.push_back(PluginEntry{.Metadata = std::move(*metadata),
                                     .Origin = origin,
                                     .SourceDir = std::move(*realDirectory),
                                     .ManifestPath = std::move(*realManifest),
                                     .ConfigureInputs = std::move(*configureInputs)});
    }
    if (error)
    {
        return Error(plugins, error.message());
    }
    return {};
}
} // namespace

std::expected<std::vector<PluginEntry>, std::string>
DiscoverPlugins(const std::filesystem::path& engineRoot,
                const std::optional<std::filesystem::path>& projectRoot) noexcept
{
    auto realEngine = CheckRoot(engineRoot);
    if (!realEngine)
    {
        return std::unexpected(realEngine.error());
    }
    std::optional<fs::path> realProject;
    if (projectRoot)
    {
        auto root = CheckRoot(*projectRoot);
        if (!root)
        {
            return std::unexpected(root.error());
        }
        if (*root == *realEngine)
        {
            return Error(*projectRoot, "engine and project roots point to the same real directory");
        }
        realProject = std::move(*root);
    }
    std::vector<PluginEntry> plugins;
    std::map<std::string, fs::path> ids;
    std::set<fs::path> visited;
    std::error_code error;
    const auto engineInput = fs::absolute(engineRoot, error);
    if (error)
    {
        return Error(engineRoot, error.message());
    }
    if (auto result = ScanRoot(engineInput, *realEngine, PluginOrigin::kEngine, plugins, ids, visited); !result)
    {
        return std::unexpected(result.error());
    }
    if (realProject)
    {
        const auto projectInput = fs::absolute(*projectRoot, error);
        if (error)
        {
            return Error(*projectRoot, error.message());
        }
        if (auto result = ScanRoot(projectInput, *realProject, PluginOrigin::kProject, plugins, ids, visited); !result)
        {
            return std::unexpected(result.error());
        }
    }
    std::ranges::sort(plugins, {}, [](const PluginEntry& entry) -> const std::string& { return entry.Metadata.Id; });
    return plugins;
}
} // namespace pg::build::plugin
