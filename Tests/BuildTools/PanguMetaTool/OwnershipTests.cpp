#include "Driver/GenerationRequest.h"
#include "Driver/OutputMapping.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <system_error>
namespace
{
using namespace pg::meta::tool;
using Path = std::filesystem::path;
class MetaOwnership : public testing::Test
{
protected:
    // GTest 派生用例共享每例隔离状态。
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    Path Root;
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    GenerationRequest Request;
    void SetUp() override
    {
        Root = std::filesystem::current_path() / "OwnershipFixture" /
               testing::UnitTest::GetInstance()->current_test_info()->name();
        std::filesystem::create_directories(Root / "A");
        std::filesystem::create_directories(Root / "B");
        std::ofstream(Root / "A" / "Player.h") << "";
        std::ofstream(Root / "B" / "Player.h") << "";
        Request.OutputRoot = Root / "Build" / "Generated" / "PanguMeta" / "Debug" / "Game";
        Request.EngineBuild = Root / "Build";
        std::filesystem::create_directories(Request.OutputRoot);
        Request.Headers = {{.Path = Root / "A" / "Player.h", .Root = Root / "A", .Prefix = {}, .TypedAccess = true}};
        Request.Outputs = {"Public/Player.gen.h"};
    }
    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove_all(Root, error);
    }
};
} // namespace
TEST_F(MetaOwnership, OutputMappingIsInjective)
{
    ASSERT_TRUE(BuildOutputMapping(Request));
    Request.Headers.push_back({.Path = Root / "B" / "Player.h", .Root = Root / "B", .Prefix = {}, .TypedAccess = true});
    EXPECT_FALSE(ValidateOutputMapping(Request));
    Request.Headers.back().Prefix = "Extra";
    Request.Outputs.emplace_back("Public/Extra/Player.gen.h");
    ASSERT_TRUE(BuildOutputMapping(Request));
    Request.Headers.front().Prefix = "Extra";
    EXPECT_FALSE(ValidateOutputMapping(Request));
    Request.Headers.front().Prefix = "../escape";
    EXPECT_FALSE(ValidateOutputMapping(Request));
    Request.Headers.front().Prefix.clear();
    Request.Headers.front().Root = Root / "B";
    EXPECT_FALSE(ValidateOutputMapping(Request));
    Request.Headers.front().Root = Root / "A";
    Request.Headers.back().Prefix = "extra";
    Request.Headers.front().Prefix = "Extra";
    Request.Outputs = {"Public/Extra/Player.gen.h", "Public/extra/Player.gen.h"};
#ifdef __linux__
    // 正式 Linux BuildRoot 使用未启用 casefold 的 ext4，两者是不同槽位。
    EXPECT_TRUE(ValidateOutputMapping(Request));
#else
    EXPECT_FALSE(ValidateOutputMapping(Request));
#endif
}
TEST_F(MetaOwnership, FixedSlotsAndConsumption)
{
    Request.Headers.front().Description = true;
    Request.Outputs = {
        "Public/Player.gen.h",
        "Private/Player.gen.cpp",
        "Private/ModuleMeta.gen.h",
        "Private/ModuleMeta.gen.cpp",
    };
    const auto result = BuildOutputMapping(Request);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->size(), 4);
    std::ofstream(Root / "A" / "ModuleMeta.h") << "";
    Request.Headers.front().Path = Root / "A" / "ModuleMeta.h";
    EXPECT_FALSE(ValidateOutputMapping(Request));
    Request.Headers.clear();
    Request.Kind = TaskKind::kQuery;
    Request.Outputs = {"Private/ModuleQuery.gen.cpp"};
    EXPECT_TRUE(BuildOutputMapping(Request));
    Request.Kind = TaskKind::kCatalog;
    Request.Outputs = {"Private/StaticModuleCatalog.gen.cpp"};
    EXPECT_TRUE(BuildOutputMapping(Request));
    Request.Outputs.emplace_back("Meta.stamp");
    EXPECT_FALSE(ValidateOutputMapping(Request));
}
TEST_F(MetaOwnership, ExistingAliases)
{
    ASSERT_TRUE(BuildOutputMapping(Request));
    std::filesystem::create_directories(Request.OutputRoot / "Public");
    std::ofstream(Request.OutputRoot / "Public" / "Player.gen.h") << "";
    std::filesystem::create_directories(Request.OutputRoot / "Public" / "Extra");
    std::error_code error;
    std::filesystem::create_hard_link(Request.OutputRoot / "Public" / "Player.gen.h",
                                      Request.OutputRoot / "Public" / "Extra" / "Player.gen.h", error);
    ASSERT_FALSE(error) << error.message();
    Request.Headers.push_back(
        {.Path = Root / "B" / "Player.h", .Root = Root / "B", .Prefix = "Extra", .TypedAccess = true});
    Request.Outputs.emplace_back("Public/Extra/Player.gen.h");
    EXPECT_FALSE(ValidateOutputMapping(Request));

    ASSERT_TRUE(std::filesystem::remove(Request.OutputRoot / "Public" / "Extra" / "Player.gen.h", error));
    ASSERT_FALSE(error) << error.message();
    Request.Headers.pop_back();
    Request.Outputs.pop_back();
    std::filesystem::create_directories(Request.OutputRoot / "Private");
    for (const auto* reserved : {
             "Private/ModuleMeta.gen.h",
             "Private/ModuleMeta.gen.cpp",
             "Private/ModuleQuery.gen.cpp",
             "Private/StaticModuleCatalog.gen.cpp",
         })
    {
        SCOPED_TRACE(reserved);
        const auto destination = Request.OutputRoot / reserved;
        std::filesystem::create_hard_link(Request.OutputRoot / "Public" / "Player.gen.h", destination, error);
        ASSERT_FALSE(error) << error.message();
        const auto collision = ValidateOutputMapping(Request);
        EXPECT_FALSE(collision);
        if (!collision)
        {
            EXPECT_EQ(collision.error().front().Code, "output_collision");
        }
        ASSERT_TRUE(std::filesystem::remove(destination, error));
        ASSERT_FALSE(error) << error.message();
        // 同路径改成独立文件后，保留名本身不应误伤普通输出。
        std::ofstream(destination) << "";
        EXPECT_TRUE(ValidateOutputMapping(Request));
    }

    Request.Headers.front().Description = true;
    Request.Outputs = {
        "Public/Player.gen.h",
        "Private/Player.gen.cpp",
        "Private/ModuleMeta.gen.h",
        "Private/ModuleMeta.gen.cpp",
    };
    EXPECT_TRUE(ValidateOutputMapping(Request));
    Request.Headers.clear();
    Request.Kind = TaskKind::kQuery;
    Request.Outputs = {"Private/ModuleQuery.gen.cpp"};
    EXPECT_TRUE(ValidateOutputMapping(Request));
    Request.Kind = TaskKind::kCatalog;
    Request.Outputs = {"Private/StaticModuleCatalog.gen.cpp"};
    EXPECT_TRUE(ValidateOutputMapping(Request));
}
TEST_F(MetaOwnership, ParentFileConflict)
{
    Request.Headers.push_back(
        {.Path = Root / "B" / "Player.h", .Root = Root / "B", .Prefix = "Player.gen.h", .TypedAccess = true});
    Request.Outputs.emplace_back("Public/Player.gen.h/Player.gen.h");
    EXPECT_FALSE(ValidateOutputMapping(Request));
}
TEST_F(MetaOwnership, UnicodeNamesAndCleanup)
{
    const Path directory(u8"目录");
    const Path name(u8"角色.h");
    std::filesystem::create_directories(Root / "A" / directory);
    std::ofstream(Root / "A" / directory / name) << "";
    Request.Headers.front().Path = Root / "A" / directory / name;
    Request.Outputs = {Path(u8"Public/目录/角色.gen.h")};
    const auto unicodeMapping = ValidateOutputMapping(Request);
    ASSERT_TRUE(unicodeMapping) << (unicodeMapping ? ""
                                                   : unicodeMapping.error().front().Message + " " +
                                                         unicodeMapping.error().front().Source.File);
    EXPECT_TRUE(std::filesystem::is_empty(Request.OutputRoot));
    std::filesystem::create_directories(Root / "B" / directory);
    std::ofstream(Root / "B" / directory / name) << "";
    Request.Headers.push_back(
        {.Path = Root / "B" / directory / name, .Root = Root / "B", .Prefix = "Extra", .TypedAccess = true});
    Request.Outputs.emplace_back(u8"Public/Extra/目录/角色.gen.h");
    EXPECT_TRUE(ValidateOutputMapping(Request));
    EXPECT_TRUE(std::filesystem::is_empty(Request.OutputRoot));
    Request.Headers.back().Prefix.clear();
    EXPECT_FALSE(ValidateOutputMapping(Request));
    EXPECT_TRUE(std::filesystem::is_empty(Request.OutputRoot));
    Request.Headers.front().Prefix = "\xC3\x84";
    Request.Headers.back().Prefix = "\xC3\xA4";
    Request.Outputs = {Path(u8"Public/Ä/目录/角色.gen.h"), Path(u8"Public/ä/目录/角色.gen.h")};
#if defined(_WIN32) || defined(__APPLE__)
    EXPECT_FALSE(ValidateOutputMapping(Request));
#else
    EXPECT_TRUE(ValidateOutputMapping(Request));
#endif
    EXPECT_TRUE(std::filesystem::is_empty(Request.OutputRoot));
}
TEST_F(MetaOwnership, ScanQueryAndReservedSlots)
{
    Request.Headers.front().TypedAccess = false;
    Request.Headers.front().Description = true;
    Request.Outputs = {
        "Public/Player.gen.h",
        "Private/Player.gen.cpp",
        "Private/ModuleMeta.gen.h",
        "Private/ModuleMeta.gen.cpp",
    };
    ASSERT_TRUE(BuildOutputMapping(Request));
    Request.Module = {
        .IdSymbol = "GameId",
        .IdentityHeader = "GameId.h",
        .QuerySymbol = "QueryGame",
        .QueryHeader = "GameQuery.h",
        .ExportMacro = {},
    };
    Request.Outputs.emplace_back("Private/ModuleQuery.gen.cpp");
    ASSERT_TRUE(BuildOutputMapping(Request));
    Request.Outputs.emplace_back("Private/other.gen.cpp");
    EXPECT_FALSE(BuildOutputMapping(Request));
    Request.Outputs.pop_back();
    Request.Module.QuerySymbol.clear();
    EXPECT_FALSE(BuildOutputMapping(Request));
    Request.Headers.front().Path = Root / "A" / "ModuleMeta.h";
    std::ofstream(Request.Headers.front().Path) << "";
    Request.Headers.front().TypedAccess = true;
    Request.Headers.front().Description = false;
    Request.Headers.front().Visibility = HeaderVisibility::kPrivate;
    Request.Outputs = {"Private/ModuleMeta.gen.h"};
    EXPECT_FALSE(BuildOutputMapping(Request));
    Request.Headers.front().Prefix = "Scoped";
    Request.Outputs = {"Private/Scoped/ModuleMeta.gen.h"};
    EXPECT_TRUE(BuildOutputMapping(Request));
}
TEST_F(MetaOwnership, DirectoryAliasesAndEscape)
{
    std::filesystem::create_directories(Request.OutputRoot / "Public");
    std::error_code error;
    std::filesystem::create_directory_symlink(Request.OutputRoot / "Public", Request.OutputRoot / "Private", error);
    ASSERT_FALSE(error) << error.message();
    Request.Headers.front().Path = Root / "A" / "ModuleMeta.h";
    std::ofstream(Request.Headers.front().Path) << "";
    Request.Outputs = {"Public/ModuleMeta.gen.h"};
    EXPECT_FALSE(ValidateOutputMapping(Request));
    std::filesystem::create_directory_symlink(Root / "B", Root / "A" / "Outside", error);
    ASSERT_FALSE(error) << error.message();
    Request.Headers.front().Path = Root / "A" / "Outside" / "Player.h";
    Request.Outputs = {"Public/Outside/Player.gen.h"};
    EXPECT_FALSE(ValidateOutputMapping(Request));
}
