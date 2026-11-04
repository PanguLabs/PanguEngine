#include "PluginDiscovery.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
[[nodiscard]] bool Write(const std::filesystem::path& path, std::string_view value)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
        return false;
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(value.data(), static_cast<std::streamsize>(value.size()));
    stream.close();
    return stream.good();
}

[[nodiscard]] bool AddPlugin(const std::filesystem::path& root, std::string_view directoryName, std::string_view id,
                             bool hasManifest = true, bool hasCMake = true)
{
    const auto source = root / "Plugins" / directoryName;
    std::error_code error;
    std::filesystem::create_directories(source, error);
    if (error)
    {
        return false;
    }
    if (hasManifest && !Write(source / "Plugin.toml", "[plugin]\nid = '" + std::string(id) + "'\n"))
    {
        return false;
    }
    return !hasCMake || Write(source / "CMakeLists.txt", "cmake_minimum_required(VERSION 4.0)\n");
}

[[nodiscard]] bool ExpectFail(const std::filesystem::path& engine, const std::filesystem::path& project,
                              std::string_view label)
{
    const auto result = pg::build::plugin::DiscoverPlugins(engine, project);
    if (result)
    {
        std::cerr << label << ": invalid discovery succeeded\n";
        return false;
    }
    if (result.error().empty())
    {
        std::cerr << label << ": missing diagnostic\n";
        return false;
    }
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        return 2;
    }
    const std::filesystem::path root = argv[1];
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error)
    {
        return 2;
    }
    bool passed = true;
    const auto engine = root / "sorted/Engine";
    const auto project = root / "sorted/Project";
    passed &= AddPlugin(engine, "Physics", "pangu.physics");
    passed &= AddPlugin(project, "Gameplay", "mygame.gameplay");
    passed &= Write(engine / "Plugins/README.txt", "ordinary file is ignored");
    std::filesystem::create_directories(project / "Plugins/ordinary", error);
    passed &= !error;
    const auto sorted = pg::build::plugin::DiscoverPlugins(engine, project);
    if (!sorted || sorted->size() != 2 || (*sorted)[0].Metadata.Id != "mygame.gameplay" ||
        (*sorted)[1].Metadata.Id != "pangu.physics")
    {
        std::cerr << "Expected sorted plugins from both roots\n";
        passed = false;
    }

    const auto empty = pg::build::plugin::DiscoverPlugins(root / "empty", std::nullopt);
    if (empty)
    {
        std::cerr << "Missing engine root should be rejected\n";
        passed = false;
    }
    std::filesystem::create_directories(root / "empty-valid", error);
    const auto emptyValid = pg::build::plugin::DiscoverPlugins(root / "empty-valid", std::nullopt);
    if (!emptyValid || !emptyValid->empty())
    {
        std::cerr << "Engine with no Plugins directory should yield empty list\n";
        passed = false;
    }
    passed &= ExpectFail(engine, engine, "Same actual root twice");
    const auto duplicateEngine = root / "duplicate/Engine";
    const auto duplicateProject = root / "duplicate/Project";
    passed &= AddPlugin(duplicateEngine, "Physics", "shared.id");
    passed &= AddPlugin(duplicateProject, "Unselected", "shared.id");
    passed &= ExpectFail(duplicateEngine, duplicateProject, "Duplicate disabled plugin id");

    const auto sharedFolderEngine = root / "same-folder/Engine";
    const auto sharedFolderProject = root / "same-folder/Project";
    passed &= AddPlugin(sharedFolderEngine, "Physics", "engine.physics");
    passed &= AddPlugin(sharedFolderProject, "Physics", "project.physics");
    const auto sharedFolders = pg::build::plugin::DiscoverPlugins(sharedFolderEngine, sharedFolderProject);
    if (!sharedFolders || sharedFolders->size() != 2)
    {
        std::cerr << "Same folder name under independent roots must be allowed\n";
        passed = false;
    }

    const auto missingManifest = root / "missing-manifest/Engine";
    passed &= AddPlugin(missingManifest, "Broken", "unused.id", false);
    passed &= ExpectFail(missingManifest, project, "Only CMakeLists.txt");
    const auto missingCMake = root / "missing-cmake/Engine";
    passed &= AddPlugin(missingCMake, "Broken", "unused.id", true, false);
    passed &= ExpectFail(missingCMake, project, "Only Plugin.toml");
    const auto wrongType = root / "wrong-type/Engine";
    passed &= AddPlugin(wrongType, "Broken", "unused.id", true, false);
    std::filesystem::create_directories(wrongType / "Plugins/Broken/CMakeLists.txt", error);
    passed &= !error;
    passed &= ExpectFail(wrongType, project, "Directory instead of CMakeLists file");
    const auto invalidManifest = root / "invalid-manifest/Engine";
    passed &= AddPlugin(invalidManifest, "Unselected", "Pangu.Bad");
    passed &= ExpectFail(invalidManifest, project, "Invalid disabled manifest");

    const auto escapeEngine = root / "symlink/Engine";
    const auto external = root / "symlink/External/Plugins/Physics";
    passed &= AddPlugin(root / "symlink/External", "Physics", "escaped.id");
    std::filesystem::create_directories(escapeEngine / "Plugins", error);
    passed &= !error;
    std::filesystem::create_directory_symlink(external, escapeEngine / "Plugins/Escaped", error);
    if (!error)
    {
        passed &= ExpectFail(escapeEngine, project, "Symlink path escape");
    }
    else
    {
        std::cerr << "Symlink test skipped: " << error.message() << '\n';
    }
    return passed ? 0 : 1;
}
