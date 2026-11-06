#include "DirectoryLink.h"
#include "PluginDiscovery.h"
#include "TestWorkspace.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
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
    std::ofstream stream(path, std::ios::binary);
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

class PluginDiscovery : public testing::Test
{
protected:
    void SetUp() override
    {
        std::error_code error;
        const auto work = std::filesystem::current_path(error);
        ASSERT_FALSE(error);
        const auto* info = testing::UnitTest::GetInstance()->current_test_info();
        const auto workspace = pg::test::CreateTestWorkspace(work, info->name());
        ASSERT_TRUE(workspace);
        Workspace = *workspace;
        std::filesystem::remove_all(Workspace, error);
        ASSERT_FALSE(error);
        std::filesystem::create_directories(Workspace, error);
        ASSERT_FALSE(error);
    }

    [[nodiscard]] const std::filesystem::path& Root() const { return Workspace; }

private:
    std::filesystem::path Workspace;
};

TEST_F(PluginDiscovery, SortsBothRootsAndIgnoresOrdinaryEntries)
{
    const auto engine = Root() / "Engine";
    const auto project = Root() / "Project";
    ASSERT_TRUE(AddPlugin(engine, "Physics", "pangu.physics"));
    ASSERT_TRUE(AddPlugin(project, "Gameplay", "mygame.gameplay"));
    ASSERT_TRUE(Write(engine / "Plugins/README.txt", "ordinary file is ignored"));
    std::error_code error;
    std::filesystem::create_directories(project / "Plugins/ordinary", error);
    ASSERT_FALSE(error);
    const auto sorted = pg::build::plugin::DiscoverPlugins(engine, project);
    ASSERT_TRUE(sorted);
    ASSERT_EQ(sorted->size(), 2U);
    EXPECT_EQ(sorted->front().Metadata.Id, "mygame.gameplay");
    EXPECT_EQ(sorted->back().Metadata.Id, "pangu.physics");
}

TEST_F(PluginDiscovery, RejectsMissingEngineRoot)
{
    const auto result = pg::build::plugin::DiscoverPlugins(Root() / "missing", std::nullopt);
    ASSERT_FALSE(result);
    EXPECT_FALSE(result.error().empty());
}

TEST_F(PluginDiscovery, AllowsEngineWithoutPluginsDirectory)
{
    const auto result = pg::build::plugin::DiscoverPlugins(Root(), std::nullopt);
    ASSERT_TRUE(result);
    EXPECT_TRUE(result->empty());
}

TEST_F(PluginDiscovery, RejectsSameActualRootTwice)
{
    ASSERT_TRUE(AddPlugin(Root(), "Physics", "pangu.physics"));
    const auto result = pg::build::plugin::DiscoverPlugins(Root(), Root());
    ASSERT_FALSE(result);
    EXPECT_FALSE(result.error().empty());
}

TEST_F(PluginDiscovery, RejectsDuplicateDisabledPluginIds)
{
    const auto engine = Root() / "Engine";
    const auto project = Root() / "Project";
    ASSERT_TRUE(AddPlugin(engine, "Physics", "shared.id"));
    ASSERT_TRUE(AddPlugin(project, "Unselected", "shared.id"));
    const auto result = pg::build::plugin::DiscoverPlugins(engine, project);
    ASSERT_FALSE(result);
    EXPECT_FALSE(result.error().empty());
}

TEST_F(PluginDiscovery, AllowsSameFolderNameAcrossIndependentRoots)
{
    const auto engine = Root() / "Engine";
    const auto project = Root() / "Project";
    ASSERT_TRUE(AddPlugin(engine, "Physics", "engine.physics"));
    ASSERT_TRUE(AddPlugin(project, "Physics", "project.physics"));
    const auto result = pg::build::plugin::DiscoverPlugins(engine / ".", project / "Plugins/..");
    ASSERT_TRUE(result);
    ASSERT_EQ(result->size(), 2U);
    EXPECT_EQ(result->front().Origin, pg::build::plugin::PluginOrigin::kEngine);
    EXPECT_EQ(result->back().Origin, pg::build::plugin::PluginOrigin::kProject);
    const auto reversed = pg::build::plugin::DiscoverPlugins(project, engine);
    ASSERT_TRUE(reversed);
    ASSERT_EQ(reversed->size(), 2U);
    EXPECT_EQ(reversed->front().Origin, pg::build::plugin::PluginOrigin::kProject);
    EXPECT_EQ(reversed->back().Origin, pg::build::plugin::PluginOrigin::kEngine);
}

TEST_F(PluginDiscovery, RejectsMissingManifest)
{
    ASSERT_TRUE(AddPlugin(Root(), "Broken", "unused.id", false));
    const auto result = pg::build::plugin::DiscoverPlugins(Root(), std::nullopt);
    ASSERT_FALSE(result);
    EXPECT_FALSE(result.error().empty());
}

TEST_F(PluginDiscovery, RejectsMissingCMake)
{
    ASSERT_TRUE(AddPlugin(Root(), "Broken", "unused.id", true, false));
    const auto result = pg::build::plugin::DiscoverPlugins(Root(), std::nullopt);
    ASSERT_FALSE(result);
    EXPECT_FALSE(result.error().empty());
}

TEST_F(PluginDiscovery, RejectsDirectoryInPlaceOfCMakeFile)
{
    ASSERT_TRUE(AddPlugin(Root(), "Broken", "unused.id", true, false));
    std::error_code error;
    std::filesystem::create_directories(Root() / "Plugins/Broken/CMakeLists.txt", error);
    ASSERT_FALSE(error);
    const auto result = pg::build::plugin::DiscoverPlugins(Root(), std::nullopt);
    ASSERT_FALSE(result);
    EXPECT_FALSE(result.error().empty());
}

TEST_F(PluginDiscovery, RejectsInvalidDisabledManifest)
{
    ASSERT_TRUE(AddPlugin(Root(), "Unselected", "Pangu.Bad"));
    const auto result = pg::build::plugin::DiscoverPlugins(Root(), std::nullopt);
    ASSERT_FALSE(result);
    EXPECT_FALSE(result.error().empty());
}

TEST_F(PluginDiscovery, RejectsDirectoryLinkPathEscape)
{
    const auto engine = Root() / "Engine";
    const auto external = Root() / "External";
    ASSERT_TRUE(AddPlugin(external, "Physics", "escaped.id"));
    std::error_code error;
    std::filesystem::create_directories(engine / "Plugins", error);
    ASSERT_FALSE(error);
    error = pg::test::CreateDirectoryLink(external / "Plugins/Physics", engine / "Plugins/Escaped");
    ASSERT_FALSE(error) << "关键逃逸场景需要目录链接能力: " << error.message();
    const auto resolved = std::filesystem::canonical(engine / "Plugins/Escaped", error);
    ASSERT_FALSE(error);
    EXPECT_EQ(resolved, std::filesystem::canonical(external / "Plugins/Physics", error));
    ASSERT_FALSE(error);
    const auto result = pg::build::plugin::DiscoverPlugins(engine, std::nullopt);
    ASSERT_FALSE(result);
    EXPECT_FALSE(result.error().empty());
}
} // namespace
