#include "Driver/CompilationContext.h"
#include "Driver/GenerationRequest.h"
#include <algorithm>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/FileSystemOptions.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/ADT/SmallString.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
using namespace pg::meta::tool;
using Path = std::filesystem::path;
using Json = nlohmann::json;
class CapturedDiagnostics final : public clang::DiagnosticConsumer
{
public:
    std::vector<std::string> Messages;
    void HandleDiagnostic(clang::DiagnosticsEngine::Level level, const clang::Diagnostic& info) override
    {
        clang::DiagnosticConsumer::HandleDiagnostic(level, info);
        llvm::SmallString<256> message;
        info.FormatDiagnostic(message);
        Messages.emplace_back(message.c_str());
    }
};
std::string Utf8(const Path& path)
{
    const auto value = path.generic_u8string();
    return {value.begin(), value.end()};
}
class MetaContext : public testing::Test
{
protected:
    // GTest 派生用例共享每例隔离状态。
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    Path Root;
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    GenerationRequest Request;
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    HeaderInput Header;
    void SetUp() override
    {
        Root = std::filesystem::current_path() / Path(u8"上下文 Fixture") /
               testing::UnitTest::GetInstance()->current_test_info()->name();
        std::filesystem::create_directories(Root);
        std::ofstream(Root / "CMakeCache.txt") << "CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_BUILD_TYPE:STRING=Debug\n";
        std::ofstream(Root / "Context.cpp") << "";
        std::ofstream(Root / "Header.h") << "struct Value {};\n";
        Request.Target = "B";
        Request.Config = "Debug";
        Request.EngineBuild = Root;
        Request.Context = {
            .Database = Root / "compile_commands.json",
            .Source = Root / "Context.cpp",
            .Object = Root / "B.obj",
            .ResourceDir = PG_TEST_RESOURCE,
        };
        Header.Path = Root / "Header.h";
        Header.Root = Root;
    }
    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove_all(Root, error);
    }
    [[nodiscard]] Json Command(std::string object, int value) const
    {
        return {
            {"directory", Utf8(Root)},
            {"file", "Context.cpp"},
            {"output", object},
            {
                "arguments",
                {
                    PG_TEST_COMPILER,
                    "-std=c++23",
                    "-DPG_TEST_VALUE=" + std::to_string(value),
                    "-c",
                    "Context.cpp",
                    "-o",
                    object,
                },
            },
        };
    }
    void Database(const Json& entries) const { std::ofstream(Request.Context.Database) << entries.dump(); }
    [[nodiscard]] bool Parse(const std::vector<std::string>& invocation, std::string_view source)
    {
        std::ofstream(Header.Path) << source;
        auto args = invocation;
        args.insert(args.begin(), PG_TEST_COMPILER);
        CapturedDiagnostics diagnostics;
        llvm::IntrusiveRefCntPtr<clang::FileManager> const files(new clang::FileManager(clang::FileSystemOptions{}));
        clang::tooling::ToolInvocation tool(args, std::make_unique<clang::SyntaxOnlyAction>(), files.get());
        tool.setDiagnosticConsumer(&diagnostics);
        const bool ok = tool.run();
        LastMessages = diagnostics.Messages;
        return ok && diagnostics.getNumErrors() == 0;
    }
    // GTest 派生用例验证真实 Clang 诊断内容。
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    std::vector<std::string> LastMessages;
};
} // namespace

TEST_F(MetaContext, SelectsExactObject)
{
    const Json entries = Json::array({Command("A.obj", 17), Command("B.obj", 29)});
    Database(entries);
    auto result = BuildInvocation(Request, Header);
    ASSERT_TRUE(result);
    EXPECT_TRUE(Parse(*result, "static_assert(PG_TEST_VALUE == 29);"));
    Request.Context.Object = Root / "A.obj";
    result = BuildInvocation(Request, Header);
    ASSERT_TRUE(result);
    EXPECT_TRUE(Parse(*result, "static_assert(PG_TEST_VALUE == 17);"));
    Request.Context.Object.clear();
    EXPECT_FALSE(BuildInvocation(Request, Header));
    Request.Context.Object = Root / "missing.obj";
    EXPECT_FALSE(BuildInvocation(Request, Header));
    Request.Context.Object = Root / "B.obj";
    Database(Json::array({entries.at(1), entries.at(1)}));
    EXPECT_FALSE(BuildInvocation(Request, Header));
    Database(entries);
    Request.Config = "Release";
    EXPECT_FALSE(BuildInvocation(Request, Header));
}

TEST_F(MetaContext, ResponseFilesPreserveArguments)
{
    std::filesystem::create_directories(Root / Path(u8"中文 includes"));
    std::ofstream(Root / Path(u8"中文 includes") / "Value.h") << "#define FROM_HEADER 41\n";
    std::error_code copyError;
    std::filesystem::copy_file(Path(PG_TEST_FIXTURES) / "nested.rsp", Root / "nested.rsp",
                               std::filesystem::copy_options::overwrite_existing, copyError);
    ASSERT_FALSE(copyError) << copyError.message();
    std::filesystem::copy_file(Path(PG_TEST_FIXTURES) / "outer.rsp", Root / "outer.rsp",
                               std::filesystem::copy_options::overwrite_existing, copyError);
    ASSERT_FALSE(copyError) << copyError.message();
    auto entry = Command("B.obj", 29);
    entry.at("arguments") = {
        PG_TEST_COMPILER, "-std=c++23", "@outer.rsp", "-c", "Context.cpp", "-oB.obj", "-MMD", "-MF", "out.d",
    };
    Database(Json::array({entry}));
    auto result = BuildInvocation(Request, Header);
    ASSERT_TRUE(result);
    EXPECT_TRUE(Parse(*result, "#include <Value.h>\nstatic_assert(FROM_HEADER == 41 && PG_TEST_VALUE == "
                               "29);\nstatic_assert(sizeof(STRING_VALUE)==12);"));
    EXPECT_EQ(std::ranges::find(*result, "-MMD"), result->end());
    entry.erase("arguments");
    entry.emplace("command", std::string("\"") + PG_TEST_COMPILER + "\" -std=c++23 @outer.rsp -c Context.cpp -o B.obj");
    Database(Json::array({entry}));
    result = BuildInvocation(Request, Header);
    ASSERT_TRUE(result);
    EXPECT_TRUE(Parse(*result, "#include <Value.h>\nstatic_assert(sizeof(STRING_VALUE)==12);"));
    std::ofstream(Root / "nested.rsp") << "@outer.rsp";
    EXPECT_FALSE(BuildInvocation(Request, Header));
    std::ofstream(Root / "nested.rsp") << "@missing.rsp";
    EXPECT_FALSE(BuildInvocation(Request, Header));
}

TEST_F(MetaContext, WarningSuppressionCannotHidePgaTypo)
{
    auto entry = Command("B.obj", 29);
    for (const auto* flag :
         {"-w", "-Wno-everything", "-Wno-error", "-Wno-unknown-attributes", "-Wno-error=unknown-attributes"})
    {
        entry.at("arguments").push_back(flag);
    }
    Database(Json::array({entry}));
    const auto result = BuildInvocation(Request, Header);
    ASSERT_TRUE(result);
    EXPECT_TRUE(Parse(*result, "struct Value {};"));
    EXPECT_FALSE(Parse(*result, "struct [[pga::Tyoe]] Value {};"));
    EXPECT_TRUE(std::ranges::any_of(LastMessages, [](const auto& message) { return message.contains("Tyoe"); }));
    for (const auto* flag :
         {"-w", "-Wno-everything", "-Wno-error", "-Wno-unknown-attributes", "-Wno-error=unknown-attributes"})
    {
        EXPECT_EQ(std::ranges::find(*result, flag), result->end());
    }
}

TEST_F(MetaContext, RejectsUnsupportedDriverAndBinaryInputs)
{
    for (const auto* flag : {
             "--driver-mode=cl",
             "-fmodules",
             "-fmodule-file=bad.pcm",
             "-include-pch=bad.pch",
             "--pangu-unknown-option",
             "--target=invalid-unknown-none",
             "-std=invalid",
             "--warning-suppression-mappings=hidden.txt",
         })
    {
        auto entry = Command("B.obj", 29);
        entry.at("arguments").push_back(flag);
        Database(Json::array({entry}));
        EXPECT_FALSE(BuildInvocation(Request, Header)) << flag;
    }
    auto entry = Command("B.obj", 29);
    entry.at("arguments").push_back("-include");
    std::ofstream(Root / "Forced.h") << "struct Value {};";
    entry.at("arguments").push_back("Forced.h");
    Database(Json::array({entry}));
    const auto result = BuildInvocation(Request, Header);
    ASSERT_TRUE(result);
    EXPECT_TRUE(Parse(*result, "Value value;"));
}

TEST_F(MetaContext, DedicatedSourceAndConfigurationGuards)
{
    Request.OutputRoot = Root / "Generated" / "PanguMeta" / "Debug" / "B";
    Request.Context.Source = Request.OutputRoot / "Private" / "MetaContext.cpp";
    Request.Context.Object.clear();
    std::filesystem::create_directories(Request.Context.Source.parent_path());
    std::ofstream(Request.Context.Source) << "";
    auto entry = Command("B.obj", 29);
    entry.at("file") = Utf8(Request.Context.Source);
    entry.at("arguments").at(4) = Utf8(Request.Context.Source);
    Database(Json::array({entry}));
    ASSERT_TRUE(BuildInvocation(Request, Header));
    entry.at("arguments").push_back(Utf8(Request.Context.Source));
    Database(Json::array({entry}));
    EXPECT_FALSE(BuildInvocation(Request, Header));
    entry.at("arguments").erase(entry.at("arguments").end() - 1);
    entry.at("arguments").push_back("--driver-mode=cl");
    Database(Json::array({entry}));
    EXPECT_FALSE(BuildInvocation(Request, Header));
    entry.at("arguments").erase(entry.at("arguments").end() - 1);
    Database(Json::array({entry}));
    Request.OutputRoot = Root / "Generated" / "PanguMeta" / "Debug" / "A";
    EXPECT_FALSE(BuildInvocation(Request, Header));
    Request.OutputRoot = Request.Context.Source.parent_path().parent_path();
    std::ofstream(Root / "CMakeCache.txt")
        << "CMAKE_GENERATOR:INTERNAL=Ninja "
           "Multi-Config\nCMAKE_BUILD_TYPE:STRING=Debug\nCMAKE_CONFIGURATION_TYPES:STRING=Debug;Release\n";
    EXPECT_FALSE(BuildInvocation(Request, Header));
    std::ofstream(Root / "CMakeCache.txt") << "CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_BUILD_TYPE:STRING=Debug\n";
    Request.Context.ResourceDir = Root;
    EXPECT_FALSE(BuildInvocation(Request, Header));
}

TEST_F(MetaContext, TargetMacrosMatchInvocation)
{
    auto entry = Command("B.obj", 29);
    entry.at("arguments").push_back("--target=aarch64-linux-android30");
    Database(Json::array({entry}));
    auto result = BuildInvocation(Request, Header);
    ASSERT_TRUE(result);
    EXPECT_TRUE(Parse(*result, "#if !defined(__aarch64__) || !defined(__ANDROID__) || __ANDROID_API__ != 30\n#error "
                               "wrong target\n#endif\nstatic_assert(sizeof(void*) == 8);"));
    entry.at("arguments").back() = "--target=arm64-apple-ios17.0";
    Database(Json::array({entry}));
    result = BuildInvocation(Request, Header);
    ASSERT_TRUE(result);
    EXPECT_TRUE(Parse(*result, "#if !defined(__aarch64__) || !defined(__APPLE__)\n#error wrong "
                               "target\n#endif\nstatic_assert(sizeof(void*) == 8);"));
}

TEST_F(MetaContext, RawHeaderWithWarningsAsErrors)
{
    std::ofstream(Root / "Forced.h") << "#define FORCED_VALUE 47\n";
    // 若 driver 偷用旁侧 PCH，这个无效文件会使真实前端失败。
    std::ofstream(Root / "Forced.h.pch") << "not a precompiled header";
    auto entry = Command("B.obj", 29);
    entry.at("arguments").push_back("-Werror");
    entry.at("arguments").push_back("-include");
    entry.at("arguments").push_back("Forced.h");
    Database(Json::array({entry}));
    const auto result = BuildInvocation(Request, Header);
    ASSERT_TRUE(result);
    EXPECT_TRUE(Parse(*result, "#pragma once\nstatic_assert(FORCED_VALUE == 47);"));
}

TEST_F(MetaContext, CMakeCodeViewDebugContext)
{
    auto entry = Command("B.obj", 29);
    entry.at("arguments").push_back("-Werror");
    entry.at("arguments").push_back("-Xclang");
    entry.at("arguments").push_back("-gcodeview");
    Database(Json::array({entry}));
    const auto result = BuildInvocation(Request, Header);
    ASSERT_TRUE(result);
    EXPECT_TRUE(Parse(*result, "static_assert(PG_TEST_VALUE == 29);"));
}
