#include "Driver/Diagnostic.h"
#include "Driver/GenerationRequest.h"
#include "Driver/InputOwnership.h"
#include "Driver/OutputMapping.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
class MetaRequest : public testing::Test
{
protected:
    // GTest 派生用例共享夹具目录，状态须保持每例隔离。
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    std::filesystem::path Root;
    void SetUp() override
    {
        Root = std::filesystem::current_path() / std::filesystem::path(u8"请求 Fixture") /
               testing::UnitTest::GetInstance()->current_test_info()->name();
        std::filesystem::create_directories(Root / "Source");
        std::ofstream(Root / "Source" / "Player.h") << "struct Player {};";
    }
    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove_all(Root, error);
    }
    [[nodiscard]] std::string Valid() const
    {
        const auto utf8 = Root.generic_u8string();
        const std::string root(utf8.begin(), utf8.end());
        return R"({"version":1,"kind":"scan","target":"Game","owner":"engine","config":"Debug","engine_source":")" +
               root + R"(/Source","engine_build":")" + root +
               R"(/Build","project_source":"","project_build":"","output_root":")" + root +
               R"(/Build/Generated/PanguMeta/Debug/Game","module":{"id_symbol":"GameId","identity_header":"GameIdentity.h","query_symbol":"QueryGame","query_header":"GameQuery.h","export_macro":"GAME_API"},"headers":[{"path":")" +
               root + R"(/Source/Player.h","root":")" + root +
               R"(/Source","prefix":"","visibility":"public","typed_access":true,"description":true}],"ownership":[{"path":")" +
               root + R"(/Source/Player.h","target":"Game"}],"context":{"database":")" + root +
               R"(/compile_commands.json","source":")" + root + R"(/MetaContext.cpp","object":"","resource_dir":")" +
               root + R"(/Resource"},"catalog":[],"outputs":["Public/Player.gen.h","Private/Player.gen.cpp"]})";
    }
    pg::meta::tool::ToolResult<pg::meta::tool::GenerationRequest> Read(const std::string& text) const
    {
        const auto path = Root / "request.json";
        {
            std::ofstream stream(path, std::ios::binary);
            stream << text;
        }
        return pg::meta::tool::ReadRequest(path);
    }
    void Reject(std::string_view from, std::string_view to) const
    {
        auto text = Valid();
        const auto index = text.find(from);
        ASSERT_NE(index, std::string::npos);
        text.replace(index, from.size(), to);
        EXPECT_FALSE(Read(text)) << to;
    }
    static void Empty(std::string& text, std::string_view member, char end)
    {
        const auto start = text.find(std::string("\"") + std::string(member) + "\":") + member.size() + 3;
        const auto length = text.find(end, start) - start + 1;
        text.replace(start, length, end == ']' ? "[]" : "{}");
    }
    [[nodiscard]] std::string Assembly(std::string_view kind) const
    {
        auto text = Valid();
        text.replace(text.find("\"scan\""), 6, std::string("\"") + std::string(kind) + "\"");
        Empty(text, "headers", ']');
        Empty(text, "context", '}');
        Empty(text, "outputs", ']');
        if (kind == "catalog")
        {
            Empty(text, "module", '}');
        }
        return text;
    }
};
} // namespace

TEST_F(MetaRequest, SchemaAndPaths)
{
    auto valid = Read(Valid());
    ASSERT_TRUE(valid);
    EXPECT_EQ(valid->Headers.front().Path, Root / "Source" / "Player.h");
    EXPECT_EQ(valid->Module.IdSymbol, "GameId");
    Reject("\"version\":1", "\"version\":2");
    Reject("\"version\":1", "\"version\":1.0");
    Reject("\"version\":1", "\"version\":-1");
    Reject("\"version\":1", "\"version\":18446744073709551616");
    Reject("\"version\":1", R"("version":1,"version":1)");
    Reject("\"version\":1", R"("version":1,"unknown":0)");
    Reject("\"typed_access\":true", "\"typed_access\":3");
    Reject(R"("id_symbol":"GameId")", "\"id_symbol\":false");
    Reject(R"("id_symbol":"GameId")", R"("id_symbol":"GameId","id_symbol":"GameId")");
    Reject("\"headers\":[", R"("headers":{} ,"unused":[)");
    Reject("Public/Player.gen.h", "/absolute.gen.h");
    Reject("Public/Player.gen.h", "../escape.gen.h");
    Reject("Public/Player.gen.h", "Private/Player.gen.cpp");
    Reject("Public/Player.gen.h", "Meta.stamp");
    Reject("Public/Player.gen.h", "Public/./Player.gen.h");
    Reject("Public/Player.gen.h", "Public/../Player.gen.h");
    Reject("/Generated/PanguMeta/Debug/Game", "/Generated/PanguMeta/Release/Game");
    Reject(R"("visibility":"public")", R"("visibility":"unknown")");
    Reject(R"("prefix":"")", R"("prefix":"../escape")");
    Reject(R"("config":"Debug")", R"("config":"../Debug")");
    Reject(R"("target":"Game")", R"("target":"")");
    Reject(R"("owner":"engine")", R"("owner":"project")");
    EXPECT_FALSE(Read("{ malformed"));
    EXPECT_FALSE(Read(std::string("{\"version\":1,\"bad\":\"") + char(0xff) + "\"}"));
    auto escapedDuplicate = Valid();
    escapedDuplicate.replace(escapedDuplicate.find("\"version\":1"), 11, R"("version":1,"vers\u0069on":1)");
    EXPECT_FALSE(Read(escapedDuplicate));
}

TEST_F(MetaRequest, AssemblyModesAndCatalogOrder)
{
    auto query = Assembly("query");
    query.replace(query.find("\"outputs\":[]"), 12, R"("outputs":["Private/ModuleQuery.gen.cpp"])");
    const auto queryRequest = Read(query);
    ASSERT_TRUE(queryRequest);
    EXPECT_EQ(queryRequest->Kind, pg::meta::tool::TaskKind::kQuery);
    EXPECT_TRUE(queryRequest->Headers.empty());
    EXPECT_TRUE(queryRequest->Context.Database.empty());
    auto catalog = Assembly("catalog");
    catalog.replace(catalog.find("\"outputs\":[]"), 12, R"("outputs":["Private/StaticModuleCatalog.gen.cpp"])");
    catalog.replace(
        catalog.find("\"catalog\":[]"), 12,
        R"("catalog":[{"target":"Z","query_symbol":"QueryZ","query_header":"Z.h"},{"target":"A","query_symbol":"QueryA","query_header":"A.h"}])");
    const auto catalogRequest = Read(catalog);
    ASSERT_TRUE(catalogRequest);
    ASSERT_EQ(catalogRequest->Catalog.size(), 2);
    EXPECT_EQ(catalogRequest->Catalog.front().Target, "Z");
    EXPECT_EQ(catalogRequest->Catalog.back().Target, "A");
    query.replace(query.find("\"context\":{}"), 12, R"("context":{"unknown":1})");
    EXPECT_FALSE(Read(query));
    catalog.replace(catalog.find("\"module\":{}"), 11, R"("module":{"unknown":1})");
    EXPECT_FALSE(Read(catalog));
}

TEST_F(MetaRequest, ExistingOutputAliases)
{
    const auto outputRoot = Root / "Build" / "Generated" / "PanguMeta" / "Debug" / "Game";
    std::filesystem::create_directories(outputRoot / "Public");
    std::filesystem::create_directories(outputRoot / "Private");
    const auto header = outputRoot / "Public" / "Player.gen.h";
    const auto source = outputRoot / "Private" / "Player.gen.cpp";
    std::ofstream(header) << "generated";
    std::error_code error;
    std::filesystem::create_hard_link(header, source, error);
    ASSERT_FALSE(error);
    const auto request = Read(Valid());
    ASSERT_FALSE(request);
    EXPECT_EQ(request.error().front().Code, "output_collision");
#ifdef _WIN32
    // 文件已经存在时，由 filesystem 的实际身份处理大小写别名。
    Reject("Public/Player.gen.h", "Private/player.gen.cpp");
#endif
    std::filesystem::remove(source);
    std::filesystem::create_hard_link(header, outputRoot / "Meta.stamp", error);
    ASSERT_FALSE(error);
    const auto stateAlias = Read(Valid());
    ASSERT_FALSE(stateAlias);
    EXPECT_EQ(stateAlias.error().front().Code, "output_collision");
}

TEST_F(MetaRequest, MissingOutputsAreProspective)
{
    const auto outputRoot = Root / "Build/Generated/PanguMeta/Debug/Game";
    ASSERT_FALSE(std::filesystem::exists(outputRoot));
    const auto first = Read(Valid());
    ASSERT_TRUE(first);
    std::filesystem::create_directories(outputRoot / "Public");
    std::ofstream(outputRoot / "Public/Player.gen.h") << "previous output";
    const auto partial = Read(Valid());
    ASSERT_TRUE(partial);
    EXPECT_FALSE(std::filesystem::exists(outputRoot / "Private/Player.gen.cpp"));
    EXPECT_FALSE(std::filesystem::exists(outputRoot / "Meta.stamp"));
}

TEST_F(MetaRequest, OwnershipAliasesAndLifetime)
{
    using namespace pg::meta::tool;
    const auto header = Root / "Source" / "Player.h";
    auto ownership = [&]
    {
        const std::vector<OwnershipEntry> entries{{.Path = header, .Target = "Game"}};
        return InputOwnership::Create(entries);
    }();
    ASSERT_TRUE(ownership);
    const auto found = ownership->Find(Root / "Source" / "." / "Player.h");
    ASSERT_TRUE(found);
    ASSERT_TRUE(*found);
    EXPECT_EQ(found->value_or(""), "Game");
    const auto missing = ownership->Find(Root / "Source" / "Missing.h");
    ASSERT_TRUE(missing);
    EXPECT_FALSE(*missing);
    const auto alias = Root / "Source" / "Alias.h";
    std::error_code error;
    std::filesystem::create_hard_link(header, alias, error);
    ASSERT_FALSE(error);
    const auto aliased = ownership->Find(alias);
    ASSERT_TRUE(aliased);
    ASSERT_TRUE(*aliased);
    EXPECT_EQ(aliased->value_or(""), "Game");
    const std::vector<OwnershipEntry> duplicates{
        {.Path = header, .Target = "Game"},
        {.Path = alias, .Target = "Other"},
    };
    const auto duplicate = InputOwnership::Create(duplicates);
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().front().Code, "duplicate_owner");
}

// 该目标只链接 Model，确保槽位计算不会反向引入 Frontend/Clang。
TEST_F(MetaRequest, PureOutputMapping)
{
    auto request = Read(Valid());
    ASSERT_TRUE(request);
    request->Headers.front().TypedAccess = true;
    request->Headers.front().Description = true;
    request->Outputs = {
        "Public/Player.gen.h",
        "Private/Player.gen.cpp",
        "Private/ModuleMeta.gen.h",
        "Private/ModuleMeta.gen.cpp",
    };
    const auto mapping = pg::meta::tool::BuildOutputMapping(*request);
    ASSERT_TRUE(mapping);
    EXPECT_EQ(mapping->size(), 4);
}
