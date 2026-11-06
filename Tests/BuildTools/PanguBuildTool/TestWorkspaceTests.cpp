#include "DirectoryLink.h"
#include "TestWorkspace.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <system_error>

namespace
{
TEST(TestWorkspace, CreatesDirectoryInsideExplicitRoot)
{
    std::error_code error;
    const auto root = std::filesystem::current_path(error) / "workspace-root";
    ASSERT_FALSE(error);
    const auto result = pg::test::CreateTestWorkspace(root, "normal/nested");
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, root / "normal/nested");
    EXPECT_TRUE(std::filesystem::is_directory(*result, error));
    EXPECT_FALSE(error);
}

TEST(TestWorkspace, RejectsParentTraversalWithoutWritingOutsideRoot)
{
    std::error_code error;
    const auto work = std::filesystem::current_path(error);
    ASSERT_FALSE(error);
    const auto result = pg::test::CreateTestWorkspace(work / "traversal-root", "../outside");
    EXPECT_FALSE(result);
    EXPECT_FALSE(std::filesystem::exists(work / "outside", error));
    EXPECT_FALSE(error);
}

TEST(TestWorkspace, RejectsNativeAbsolutePathWithoutWriting)
{
    std::error_code error;
    const auto work = std::filesystem::current_path(error);
    ASSERT_FALSE(error);
    const auto outside = work / "absolute-outside";
    const auto result = pg::test::CreateTestWorkspace(work / "absolute-root", outside.string());
    EXPECT_FALSE(result);
    EXPECT_FALSE(std::filesystem::exists(outside, error));
    EXPECT_FALSE(error);
}

TEST(TestWorkspace, RejectsDirectoryLinkEscape)
{
    std::error_code error;
    const auto work = std::filesystem::current_path(error);
    ASSERT_FALSE(error);
    const auto root = work / "symlink-root";
    const auto outside = work / "symlink-outside";
    std::filesystem::create_directories(root, error);
    ASSERT_FALSE(error);
    std::filesystem::create_directories(outside, error);
    ASSERT_FALSE(error);
    std::filesystem::remove(root / "link", error);
    ASSERT_FALSE(error);
    error = pg::test::CreateDirectoryLink(outside, root / "link");
    ASSERT_FALSE(error) << "关键路径约束需要目录链接能力: " << error.message();
    const auto resolved = std::filesystem::canonical(root / "link", error);
    ASSERT_FALSE(error);
    EXPECT_EQ(resolved, std::filesystem::canonical(outside, error));
    ASSERT_FALSE(error);
    const auto result = pg::test::CreateTestWorkspace(root, "link/escaped");
    EXPECT_FALSE(result);
    EXPECT_FALSE(std::filesystem::exists(outside / "escaped", error));
    EXPECT_FALSE(error);
}
} // namespace
