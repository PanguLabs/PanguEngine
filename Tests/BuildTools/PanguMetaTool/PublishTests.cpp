#include "Driver/Diagnostic.h"
#include "Driver/GenerationRequest.h"
#include "Output/GeneratedFile.h"
#include "Output/Publish.h"
#include "Output/PublishInternal.h"
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <ios>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h> // IWYU pragma: keep
#include <fileapi.h>
#include <handleapi.h>
#include <winbase.h>
#include <winnt.h>
#undef ReplaceFile
#else
#include <csignal>
// SIGXFSZ 是 POSIX 扩展，直接包含定义它的系统头，csignal 提供 std::signal。
// NOLINTNEXTLINE(modernize-deprecated-headers)
#include <signal.h>
#include <sys/resource.h>
#endif

namespace
{
using namespace pg::meta::tool;
using Path = std::filesystem::path;
testing::AssertionResult Success(const ToolResult<void>& result)
{
    if (result)
    {
        return testing::AssertionSuccess();
    }
    return testing::AssertionFailure() << result.error().front().Code << ": " << result.error().front().Message << " "
                                       << result.error().front().Source.File;
}
class MetaPublish : public testing::Test
{
protected:
    // GTest 用例通过派生夹具访问隔离状态。
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    Path Root;
    GenerationRequest Request;
    std::vector<GeneratedFile> Files;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
    void SetUp() override
    {
        Root = std::filesystem::current_path() / "Publish";
        Request.Version = 1;
        Request.Target = "Game";
        Request.Config = "Debug";
        Request.EngineBuild = Root / "Build";
        Request.OutputRoot = Request.EngineBuild / "Generated/PanguMeta/Debug/Game";
        SetFiles({
            {.RelativePath = "Public/A.gen.h", .Content = "A0"},
            {.RelativePath = "Private/B.gen.cpp", .Content = "B0"},
        });
    }
    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove_all(Root, error);
    }
    void SetFiles(std::vector<GeneratedFile> files)
    {
        Files = std::move(files);
        Request.Outputs.clear();
        for (const auto& file : Files)
        {
            Request.Outputs.push_back(file.RelativePath);
        }
    }
    static void Write(const Path& path, const std::string& content)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << content;
    }
    static std::string Read(const Path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), {}};
    }
    [[nodiscard]] ToolResult<void> Run(const PublishOptions& options = {}) const
    {
        return Publish(Request, Files, {}, options);
    }
};

TEST_F(MetaPublish, SameContentKeepsTimestamp)
{
    ASSERT_TRUE(Success(Run()));
    const auto output = Request.OutputRoot / Files.front().RelativePath;
    const auto stamp = Request.OutputRoot / "Meta.stamp";
    const auto old = std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
    std::filesystem::last_write_time(output, old);
    std::filesystem::last_write_time(stamp, old);
    ASSERT_TRUE(Success(Run()));
    EXPECT_EQ(std::filesystem::last_write_time(output), old);
    EXPECT_GT(std::filesystem::last_write_time(stamp), old);
}
TEST_F(MetaPublish, PartialCommitCannotReuseOldStamp)
{
    ASSERT_TRUE(Success(Run()));
    Files.at(0).Content = "A1";
    Files.at(1).Content = "B1";
    const PublishOptions fail{
        .BeforeReplace = [](const Path& path, std::size_t) -> ToolResult<void>
        {
            if (path.filename() == "B.gen.cpp")
            {
                return std::unexpected(Diagnostics{{.Code = "publish_failed", .Message = "受控中断"}});
            }
            return {};
        },
    };
    EXPECT_FALSE(Run(fail));
    EXPECT_FALSE(std::filesystem::exists(Request.OutputRoot / "Meta.stamp"));
    EXPECT_EQ(Read(Request.OutputRoot / "Public/A.gen.h"), "A1");
    ASSERT_TRUE(Success(Run()));
    EXPECT_EQ(Read(Request.OutputRoot / "Private/B.gen.cpp"), "B1");
    EXPECT_TRUE(std::filesystem::exists(Request.OutputRoot / "Meta.stamp"));
}
TEST_F(MetaPublish, ForeignFilePreserved)
{
    ASSERT_TRUE(Success(Run()));
    Write(Request.OutputRoot / "foreign.txt", "foreign");
    SetFiles({});
    ASSERT_TRUE(Success(Run()));
    EXPECT_FALSE(std::filesystem::exists(Request.OutputRoot / "Public/A.gen.h"));
    EXPECT_EQ(Read(Request.OutputRoot / "foreign.txt"), "foreign");
}
TEST_F(MetaPublish, InvalidIndexAndPathsRejectedBeforeMutation)
{
    ASSERT_TRUE(Success(Run()));
    const auto stamp = Read(Request.OutputRoot / "Meta.stamp");
    for (const auto* invalid : {
             "broken",
             R"({"version":2,"outputs":[]})",
             R"({"version":1,"outputs":["../victim"]})",
             R"({"version":1,"outputs":["Meta.stamp"]})",
         })
    {
        Write(Request.OutputRoot / "Meta.outputs.json", invalid);
        Files.at(0).Content = "changed";
        EXPECT_FALSE(Run());
        EXPECT_EQ(Read(Request.OutputRoot / "Public/A.gen.h"), "A0");
        EXPECT_EQ(Read(Request.OutputRoot / "Meta.stamp"), stamp);
    }
}
TEST_F(MetaPublish, CleanupFailureRetainsOwnership)
{
    ASSERT_TRUE(Success(Run()));
    const auto obsolete = Request.OutputRoot / "Private/B.gen.cpp";
    SetFiles({{.RelativePath = "Public/A.gen.h", .Content = "A1"}});
    const PublishOptions block{
        .BeforeReplace = [&](const Path& path, std::size_t) -> ToolResult<void>
        {
            if (path.filename() == "Meta.d")
            {
                std::filesystem::remove(obsolete);
                Write(obsolete / "foreign", "keep");
            }
            return {};
        },
    };
    EXPECT_FALSE(Run(block));
    EXPECT_FALSE(std::filesystem::exists(Request.OutputRoot / "Meta.stamp"));
    EXPECT_NE(Read(Request.OutputRoot / "Meta.outputs.json").find("Private/B.gen.cpp"), std::string::npos);
    EXPECT_EQ(Read(obsolete / "foreign"), "keep");
    std::filesystem::remove(obsolete / "foreign");
    std::filesystem::remove(obsolete);
    Write(obsolete, "B0");
    ASSERT_TRUE(Success(Run()));
    EXPECT_FALSE(std::filesystem::exists(obsolete));
    EXPECT_EQ(Read(Request.OutputRoot / "Public/A.gen.h"), "A1");
}
TEST_F(MetaPublish, IndexFailureAndChangedRetryPreserveAllOwnership)
{
    ASSERT_TRUE(Success(Run()));
    SetFiles({{.RelativePath = "Public/New.gen.h", .Content = "new"}});
    std::size_t indexCalls = 0;
    const PublishOptions fail{
        .BeforeReplace = [&](const Path& path, std::size_t) -> ToolResult<void>
        {
            if (path.filename() == "Meta.outputs.json" && ++indexCalls == 2)
            {
                return std::unexpected(Diagnostics{{.Code = "publish_failed", .Message = "最终 index 中断"}});
            }
            return {};
        },
    };
    EXPECT_FALSE(Run(fail));
    EXPECT_EQ(Read(Request.OutputRoot / "Public/New.gen.h"), "new");
    EXPECT_FALSE(std::filesystem::exists(Request.OutputRoot / "Meta.stamp"));
    SetFiles({});
    ASSERT_TRUE(Success(Run()));
    EXPECT_FALSE(std::filesystem::exists(Request.OutputRoot / "Public/New.gen.h"));
}
TEST_F(MetaPublish, RootSetAndAliasValidation)
{
    ASSERT_TRUE(Success(Run()));
    const auto stamp = Read(Request.OutputRoot / "Meta.stamp");
    auto invalid = Request;
    invalid.OutputRoot = Root / "outside";
    EXPECT_FALSE(Publish(invalid, Files, {}));
    invalid = Request;
    invalid.Outputs.pop_back();
    EXPECT_FALSE(Publish(invalid, Files, {}));
    SetFiles({{.RelativePath = "../outside", .Content = "bad"}});
    EXPECT_FALSE(Run());
    SetFiles({{.RelativePath = "meta.STAMP", .Content = "bad"}});
    EXPECT_FALSE(Run());
    SetFiles({{.RelativePath = "Public/A.gen.h", .Content = "changed"}});
    std::filesystem::remove(Request.OutputRoot / "Meta.d");
    std::filesystem::create_hard_link(Request.OutputRoot / "Public/A.gen.h", Request.OutputRoot / "Meta.d");
    EXPECT_FALSE(Run());
    EXPECT_EQ(Read(Request.OutputRoot / "Meta.stamp"), stamp);
    EXPECT_EQ(Read(Request.OutputRoot / "Public/A.gen.h"), "A0");
}
TEST_F(MetaPublish, LinksCannotEscapeOrDeleteForeignFiles)
{
    ASSERT_TRUE(Success(Run()));
    const auto outside = Root / "outside";
    Write(outside / "A.gen.h", "foreign");
    const auto output = Request.OutputRoot / "Public/A.gen.h";
    std::filesystem::remove(output);
    std::filesystem::remove(output.parent_path());
    std::error_code error;
    std::filesystem::create_directory_symlink(outside, output.parent_path(), error);
    ASSERT_FALSE(error) << error.message();
    EXPECT_FALSE(Run());
    SetFiles({});
    EXPECT_FALSE(Run());
    EXPECT_EQ(Read(outside / "A.gen.h"), "foreign");
}
TEST_F(MetaPublish, UnicodeAndProspectiveAliases)
{
    SetFiles({{.RelativePath = Path(u8"Public/角色/玩家.gen.h"), .Content = "unicode"}});
    ASSERT_TRUE(Success(Run()));
    EXPECT_EQ(Read(Request.OutputRoot / Files.at(0).RelativePath), "unicode");
    SetFiles({
        {.RelativePath = "Public/A.gen.h", .Content = "one"},
        {.RelativePath = "Public/A.gen.h/child", .Content = "two"},
    });
    EXPECT_FALSE(Run());
#ifdef _WIN32
    SetFiles({
        {.RelativePath = Path(u8"Public/É/玩家.gen.h"), .Content = "one"},
        {.RelativePath = Path(u8"public/é/玩家.gen.h"), .Content = "two"},
    });
    EXPECT_FALSE(Run());
    SetFiles(
        {{.RelativePath = "Public/A.gen.h", .Content = "one"}, {.RelativePath = "Public/A.gen.h.", .Content = "two"}});
    EXPECT_FALSE(Run());
    SetFiles({{.RelativePath = "Public/NUL.gen.h", .Content = "device"}});
    EXPECT_FALSE(Run());
#endif
}
TEST_F(MetaPublish, NativeDeleteFailureCanRetry)
{
    ASSERT_TRUE(Success(Run()));
    const auto obsolete = Request.OutputRoot / "Private/B.gen.cpp";
    SetFiles({{.RelativePath = "Public/A.gen.h", .Content = "A1"}});
#ifdef _WIN32
    HANDLE held = INVALID_HANDLE_VALUE;
#else
    const auto protectedPath = obsolete.parent_path();
#endif
    const PublishOptions block{
        .BeforeReplace = [&](const Path& path, std::size_t) -> ToolResult<void>
        {
            if (path.filename() == "Meta.d")
            {
#ifdef _WIN32
                held = CreateFileW(obsolete.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
#else
                std::filesystem::permissions(protectedPath,
                                             std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
#endif
            }
            return {};
        },
    };
    const auto failed = Run(block);
#ifdef _WIN32
    ASSERT_NE(held, INVALID_HANDLE_VALUE);
    CloseHandle(held);
#else
    std::filesystem::permissions(protectedPath, std::filesystem::perms::owner_all);
#endif
    EXPECT_FALSE(failed);
    EXPECT_TRUE(std::filesystem::exists(obsolete));
    EXPECT_FALSE(std::filesystem::exists(Request.OutputRoot / "Meta.stamp"));
    EXPECT_NE(Read(Request.OutputRoot / "Meta.outputs.json").find("Private/B.gen.cpp"), std::string::npos);
    ASSERT_TRUE(Success(Run()));
    EXPECT_FALSE(std::filesystem::exists(obsolete));
}
TEST_F(MetaPublish, CandidatePreparationFailurePreservesOldSuccess)
{
    ASSERT_TRUE(Success(Run()));
    const auto stamp = Read(Request.OutputRoot / "Meta.stamp");
    SetFiles({{.RelativePath = Path("Public") / (std::string(300, 'x') + ".h"), .Content = "candidate"}});
    EXPECT_FALSE(Run());
    EXPECT_EQ(Read(Request.OutputRoot / "Meta.stamp"), stamp);
    EXPECT_EQ(Read(Request.OutputRoot / "Public/A.gen.h"), "A0");
    SetFiles({{.RelativePath = "Public/A.gen.h", .Content = "fixed"}});
    ASSERT_TRUE(Success(Run()));
}
TEST_F(MetaPublish, DepfileEscapesSpecialCharacters)
{
    const std::vector<Path> dependencies{Path("C:/space #$/input.h"), Path("slash\\name.h")};
    ASSERT_TRUE(Publish(Request, Files, dependencies));
    const auto depfile = Read(Request.OutputRoot / "Meta.d");
    EXPECT_NE(depfile.find("C\\:/space\\ \\#$$/input.h"), std::string::npos);
#ifdef _WIN32
    EXPECT_NE(depfile.find("slash/name.h"), std::string::npos);
#else
    EXPECT_NE(depfile.find("slash\\\\name.h"), std::string::npos);
#endif
}
TEST_F(MetaPublish, NativeReplaceFile)
{
    std::filesystem::create_directories(Root);
    const auto destination = Root / "destination";
    const auto staged = Root / "staged";
    ASSERT_TRUE(WriteExclusive(staged, "first"));
    ASSERT_TRUE(ReplaceFile(staged, destination));
    EXPECT_EQ(Read(destination), "first");
    EXPECT_FALSE(WriteExclusive(destination, "must not truncate"));
    EXPECT_EQ(Read(destination), "first");
    EXPECT_FALSE(std::filesystem::exists(staged));
    Write(staged, "second");
    ASSERT_TRUE(ReplaceFile(staged, destination));
    EXPECT_EQ(Read(destination), "second");
    EXPECT_FALSE(ReplaceFile(staged, destination));
    EXPECT_EQ(Read(destination), "second");
    Write(staged, "third");
    std::filesystem::create_directory(Root / "directory");
    EXPECT_FALSE(ReplaceFile(staged, Root / "directory"));
    EXPECT_EQ(Read(staged), "third");
}
TEST_F(MetaPublish, EveryReplaceBoundaryCanRetry)
{
    ASSERT_TRUE(Success(Run()));
    for (std::size_t boundary = 0; boundary < 6; ++boundary)
    {
        SCOPED_TRACE(boundary);
        Files.at(0).Content = "A" + std::to_string(boundary);
        Files.at(1).Content = "B" + std::to_string(boundary);
        // 第一轮也必须改变两个文件，保证六个真实替换阶段均被命中。
        Files.at(0).Content += "changed";
        Files.at(1).Content += "changed";
        const PublishOptions fail{
            .BeforeReplace = [boundary](const Path&, std::size_t ordinal) -> ToolResult<void>
            {
                if (ordinal == boundary)
                {
                    return std::unexpected(Diagnostics{{.Code = "publish_failed", .Message = "阶段中断"}});
                }
                return {};
            },
        };
        EXPECT_FALSE(Run(fail));
        EXPECT_FALSE(std::filesystem::exists(Request.OutputRoot / "Meta.stamp"));
        ASSERT_TRUE(Success(Run()));
        EXPECT_EQ(Read(Request.OutputRoot / Files.at(0).RelativePath), Files.at(0).Content);
        EXPECT_EQ(Read(Request.OutputRoot / Files.at(1).RelativePath), Files.at(1).Content);
    }
}
#ifndef _WIN32
TEST_F(MetaPublish, CandidateWriteFailurePreservesOldSuccess)
{
    ASSERT_TRUE(Success(Run()));
    const auto stamp = Read(Request.OutputRoot / "Meta.stamp");
    Files.front().Content = std::string(4096, 'x');
    rlimit previous{};
    ASSERT_EQ(getrlimit(RLIMIT_FSIZE, &previous), 0);
    auto limited = previous;
    limited.rlim_cur = 32;
    const auto handler = std::signal(SIGXFSZ, SIG_IGN);
    ASSERT_EQ(setrlimit(RLIMIT_FSIZE, &limited), 0);
    const auto failed = Run();
    const auto restored = setrlimit(RLIMIT_FSIZE, &previous);
    std::signal(SIGXFSZ, handler);
    ASSERT_EQ(restored, 0);
    ASSERT_FALSE(failed);
    EXPECT_NE(failed.error().front().Message.find("staging"), std::string::npos);
    EXPECT_EQ(Read(Request.OutputRoot / "Meta.stamp"), stamp);
    EXPECT_EQ(Read(Request.OutputRoot / "Public/A.gen.h"), "A0");
    ASSERT_TRUE(Success(Run()));
    EXPECT_EQ(Read(Request.OutputRoot / "Public/A.gen.h"), Files.front().Content);
}
#endif
TEST_F(MetaPublish, MissingOutputAndStaleStagingRetry)
{
    ASSERT_TRUE(Success(Run()));
    std::filesystem::remove(Request.OutputRoot / Files.at(0).RelativePath);
    Write(Request.OutputRoot / ".meta-stage-abandoned/foreign", "keep");
    ASSERT_TRUE(Success(Run()));
    EXPECT_EQ(Read(Request.OutputRoot / Files.at(0).RelativePath), "A0");
    EXPECT_EQ(Read(Request.OutputRoot / ".meta-stage-abandoned/foreign"), "keep");
}
} // namespace
