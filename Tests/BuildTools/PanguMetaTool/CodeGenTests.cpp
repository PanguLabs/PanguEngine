#include "CodeGen/CppNames.h"
#include "CodeGen/Generate.h"
#include "Driver/GenerationRequest.h"
#include "MetaIR/Model.h"
#include "MetaOptions.h"
#include "Output/GeneratedFile.h"
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>

namespace
{
using namespace pg::meta::tool;
using Path = std::filesystem::path;
GenerationRequest Request(bool typed, bool description)
{
    const auto root = std::filesystem::current_path();
    GenerationRequest request;
    request.Target = "ActorModule";
    request.OutputRoot = root / "Generated/PanguMeta/Debug/ActorModule";
    request.Module = {
        .IdSymbol = "pg::test::kActorModule",
        .IdentityHeader = "Identity.h",
        .QuerySymbol = "pg::test::Query",
        .QueryHeader = "Identity.h",
        .ExportMacro = {},
    };
    request.Headers = {
        {.Path = root / "Actor.h", .Root = root, .Prefix = {}, .TypedAccess = typed, .Description = description},
    };
    if (typed || description)
    {
        request.Outputs.emplace_back("Public/Actor.gen.h");
    }
    if (description)
    {
        request.Outputs.emplace_back("Private/Actor.gen.cpp");
        request.Outputs.emplace_back("Private/ModuleMeta.gen.h");
        request.Outputs.emplace_back("Private/ModuleMeta.gen.cpp");
    }
    return request;
}
pg::meta::ir::Model Model(const GenerationRequest& request)
{
    const auto path = request.Headers.front().Path.generic_u8string();
    return {
        .ContextId = request.Target,
        .Types =
            {
                {
                    .QualifiedName = "pg::test::Actor",
                    .Id = {},
                    .Source = {.File = std::string(path.begin(), path.end()), .Line = 1, .Column = 1},
                    .Fields =
                        {
                            {.Name = "Speed", .Key = "Speed", .CppType = "float"},
                            {
                                .Name = "Secret",
                                .Key = "Secret",
                                .CppType = "int",
                                .Visibility = pg::meta::Access::kPrivate,
                            },
                        },
                },
            },
    };
}
} // namespace
TEST(MetaCodeGen, TypedAndDescriptionModes)
{
    for (const bool typed : {false, true})
    {
        for (const bool description : {false, true})
        {
            const auto request = Request(typed, description);
            const auto result = Generate(request, Model(request));
            ASSERT_TRUE(result);
            EXPECT_EQ(result->size(), request.Outputs.size());
            for (const auto& path : request.Outputs)
            {
                EXPECT_EQ(std::ranges::count(*result, path, &GeneratedFile::RelativePath), 1);
            }
            if (!description)
            {
                EXPECT_TRUE(std::ranges::none_of(*result, [](const auto& file)
                                                 { return file.RelativePath.extension() == ".cpp"; }));
            }
        }
    }
}
TEST(MetaCodeGen, EscapingIsInjective)
{
    EXPECT_EQ(EscapeIdentifier("A_B::中"), "T_A_5FB_3A_3A_E4_B8_AD");
    EXPECT_NE(EscapeIdentifier("_3A"), EscapeIdentifier(":"));
    EXPECT_EQ(QuoteCppString("a\"\\\n\001F中"), "\"a\\\"\\\\\\012\\001F中\"");
}
TEST(MetaCodeGen, RejectsMappingAndAssembly)
{
    auto request = Request(true, false);
    request.Outputs.clear();
    EXPECT_FALSE(Generate(request, Model(request)));
    request = Request(true, false);
    request.Outputs.emplace_back("Private/ModuleQuery.gen.cpp");
    ASSERT_TRUE(Generate(request, Model(request)));
    request.Kind = TaskKind::kQuery;
    request.Headers.clear();
    request.Outputs = {"Private/ModuleQuery.gen.cpp"};
    ASSERT_TRUE(Generate(request, {}));
    request.Kind = TaskKind::kCatalog;
    request.Outputs = {"Private/StaticModuleCatalog.gen.cpp"};
    ASSERT_TRUE(Generate(request, {}));
}
TEST(MetaCodeGen, AssemblyPreservesExplicitOrderAndGlobalNames)
{
    auto request = Request(false, false);
    request.Kind = TaskKind::kCatalog;
    request.Outputs = {"Private/StaticModuleCatalog.gen.cpp"};
    request.Catalog = {
        {.Target = "First", .QuerySymbol = "::pg::test::First", .QueryHeader = "First.h"},
        {.Target = "Second", .QuerySymbol = "pg::test::Second", .QueryHeader = "Second.h"},
    };
    const auto result = Generate(request, {});
    ASSERT_TRUE(result);
    const auto& text = result->front().Content;
    EXPECT_EQ(text.find("::::"), std::string::npos);
    const auto first = text.find("{::pg::test::First}");
    const auto second = text.find("{::pg::test::Second}");
    ASSERT_NE(first, std::string::npos);
    ASSERT_NE(second, std::string::npos);
    EXPECT_LT(first, second);
}
TEST(MetaCodeGen, RejectsInvalidAssemblyNames)
{
    auto request = Request(false, false);
    request.Kind = TaskKind::kQuery;
    request.Outputs = {"Private/ModuleQuery.gen.cpp"};
    request.Module.QuerySymbol = "pg::test::Query(); invalid";
    EXPECT_FALSE(Generate(request, {}));
    request.Kind = TaskKind::kCatalog;
    request.Outputs = {"Private/StaticModuleCatalog.gen.cpp"};
    request.Catalog = {{.Target = "First", .QuerySymbol = "pg::test::First", .QueryHeader = "bad\"header.h"}};
    EXPECT_FALSE(Generate(request, {}));
}
TEST(MetaCodeGen, QueryIncludesFollowCanonicalOrder)
{
    auto request = Request(false, false);
    request.Kind = TaskKind::kQuery;
    request.Headers.clear();
    request.Outputs = {"Private/ModuleQuery.gen.cpp"};
    request.Module.ExportMacro = "PG_ACTOR_API";
    const auto result = Generate(request, {});
    ASSERT_TRUE(result);
    const auto& text = result->front().Content;
    const auto exportHeader = text.find("\"ActorModuleExport.h\"");
    const auto identityHeader = text.find("\"Identity.h\"");
    ASSERT_NE(exportHeader, std::string::npos);
    ASSERT_NE(identityHeader, std::string::npos);
    EXPECT_LT(exportHeader, identityHeader);
    request.Module.QueryHeader = "ActorModuleExport.h";
    const auto duplicate = Generate(request, {});
    ASSERT_TRUE(duplicate);
    const auto& deduplicated = duplicate->front().Content;
    const auto firstExport = deduplicated.find("\"ActorModuleExport.h\"");
    const auto lastExport = deduplicated.rfind("\"ActorModuleExport.h\"");
    ASSERT_NE(firstExport, std::string::npos);
    ASSERT_NE(lastExport, std::string::npos);
    EXPECT_EQ(firstExport, lastExport);
}

TEST(MetaCodeGen, LongModuleEntryRespectsColumnLimit)
{
    auto request = Request(false, true);
    request.Target = "PanguMetaServiceFixture";
    auto model = Model(request);
    model.Types.front().QualifiedName = "pg::test::meta_fixture::ActorService";
    const auto result = Generate(request, model);
    ASSERT_TRUE(result);
    const auto file = std::ranges::find(*result, Path("Private/ModuleMeta.gen.cpp"), &GeneratedFile::RelativePath);
    ASSERT_NE(file, result->end());
    std::size_t start{};
    while (start < file->Content.size())
    {
        const auto end = file->Content.find('\n', start);
        ASSERT_NE(end, std::string::npos);
        EXPECT_LE(end - start, 120U) << file->Content.substr(start, end - start);
        start = end + 1;
    }
}
