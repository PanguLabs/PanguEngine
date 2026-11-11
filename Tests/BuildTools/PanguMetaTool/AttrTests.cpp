#include "Attr/Scan.h"
#include "Driver/Diagnostic.h"
#include "Driver/GenerationRequest.h"
#include "Driver/OutputMapping.h"
#include "MetaOptions.h"
#include <algorithm>
#include <expected>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <ios>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
using namespace pg::meta::tool;
using Path = std::filesystem::path;
std::string Utf8(const Path& path)
{
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}
std::string Errors(const ToolResult<ScanResult>& result)
{
    std::string text;
    if (!result)
    {
        for (const auto& error : result.error())
        {
            text += error.Code + ": " + error.Source.File + ": " + error.Message + "\n";
        }
    }
    return text;
}
class MetaAttr : public testing::Test
{
protected:
    // GTest 派生用例共享本例隔离状态。
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    Path Root;
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    GenerationRequest Request;
    void SetUp() override
    {
        Root = std::filesystem::current_path() / "a";
        std::filesystem::create_directories(Root);
        Request.Owner = SourceOwner::kEngine;
        Request.Target = "Sample";
        Request.Config = "Debug";
        Request.EngineSource = PG_TEST_ENGINE;
        Request.EngineBuild = Root;
        Request.OutputRoot = Root / "Generated/PanguMeta/Debug/Sample";
        std::filesystem::create_directories(Request.OutputRoot / "Private");
        std::filesystem::create_directories(Request.OutputRoot / "Public");
        Request.Context = {
            .Database = Root / "compile_commands.json",
            .Source = Root / "Context.cpp",
            .Object = Root / "Context.obj",
            .ResourceDir = PG_TEST_RESOURCE,
        };
        std::ofstream(Root / "Context.cpp") << "";
        std::ofstream(Root / "CMakeCache.txt") << "CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_BUILD_TYPE:STRING=Debug\n";
        const nlohmann::json arguments = {
            PG_TEST_COMPILER,
            "-std=c++23",
            "-I.",
            "-I" + Utf8(Path(PG_TEST_ENGINE) / "Source/Runtime/Foundation/Public"),
            "-c",
            "Context.cpp",
            "-o",
            "Context.obj",
        };
        Database(arguments);
        Request.Headers.push_back({
            .Path = Root / "Input.h",
            .Root = Root,
            .Prefix = {},
            .Visibility = HeaderVisibility::kPublic,
            .TypedAccess = true,
            .Description = true,
        });
        Request.Ownership.push_back({.Path = Root / "Input.h", .Target = "Sample"});
    }
    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove_all(Root, error);
    }
    void Database(const nlohmann::json& arguments)
    {
        std::ofstream(Request.Context.Database) << nlohmann::json::array({
                                                                             {
                                                                                 {"directory", Utf8(Root)},
                                                                                 {"file", "Context.cpp"},
                                                                                 {"output", "Context.obj"},
                                                                                 {"arguments", arguments},
                                                                             },
                                                                         })
                                                       .dump();
    }
    ToolResult<ScanResult> Parse(std::string_view source)
    {
        std::ofstream(Root / "Input.h", std::ios::binary) << source;
        Request.Outputs = {
            "Private/ModuleMeta.gen.h",
            "Private/ModuleMeta.gen.cpp",
            "Public/Input.gen.h",
            "Private/Input.gen.cpp",
        };
        const auto mapping = BuildOutputMapping(Request);
        if (!mapping)
        {
            return std::unexpected(mapping.error());
        }
        Request.Outputs = *mapping;
        return Scan(Request);
    }
    ToolResult<ScanResult> Fixture(std::string_view name)
    {
        std::ifstream const file(Path(PG_TEST_ATTR_FIXTURES) / name, std::ios::binary);
        std::ostringstream text;
        text << file.rdbuf();
        return Parse(text.str());
    }
    static std::string FixtureText(std::string_view name)
    {
        std::ifstream const file(Path(PG_TEST_ATTR_FIXTURES) / name, std::ios::binary);
        std::ostringstream text;
        text << file.rdbuf();
        return text.str();
    }
    void Reject(std::string_view source, std::string_view code)
    {
        const auto result = Parse(source);
        ASSERT_FALSE(result) << source;
        EXPECT_TRUE(std::ranges::any_of(result.error(), [code](const auto& error) { return error.Code == code; }))
            << Errors(result);
    }
};
} // namespace
TEST_F(MetaAttr, TypeWithoutId)
{
    const auto result = Fixture("TypeCases.h");
    ASSERT_TRUE(result) << Errors(result);
    ASSERT_EQ(result->Model.Types.size(), 1);
    const auto& type = result->Model.Types.front();
    EXPECT_EQ(type.QualifiedName, "sample::Actor");
    EXPECT_FALSE(type.Id);
    ASSERT_EQ(type.Fields.size(), 4);
    EXPECT_EQ(type.Fields.at(0).Key, "Health");
    EXPECT_TRUE(type.Fields.at(1).IsConst);
    EXPECT_FALSE(type.Fields.at(1).ReadOnly);
    EXPECT_EQ(type.Fields.at(2).Visibility, pg::meta::Access::kProtected);
    EXPECT_EQ(type.Fields.at(3).Visibility, pg::meta::Access::kPrivate);
}
TEST_F(MetaAttr, OptionsDefaultsAndAliases)
{
    const auto result = Fixture("OptionsCases.h");
    ASSERT_TRUE(result) << Errors(result);
    const auto& type = result->Model.Types.front();
    EXPECT_TRUE(type.Id);
    ASSERT_EQ(type.Fields.size(), 4);
    EXPECT_EQ(type.Fields.at(0).Key, "Default");
    EXPECT_EQ(type.Fields.at(1).Key, "Aggregate");
    EXPECT_FALSE(type.Fields.at(1).ReadOnly);
    EXPECT_EQ(type.Fields.at(2).Key, "别名\"\n");
    EXPECT_TRUE(type.Fields.at(2).ReadOnly);
    EXPECT_EQ(type.Fields.at(3).Key, "factory");
}
TEST_F(MetaAttr, OwnershipAndConditionGuards)
{
    Reject("struct A { [[pga::Field]] int Value; };", "orphan_field");
    Reject("struct [[pga::Type(\"bad\")]] A {};", "invalid_type_id");
    Reject("struct [[pga::Type]] A { [[pga::Field(1)]] int Value; };", "invalid_options");
    Reject("struct [[pga::Type,pga::Type]] A {};", "unsupported_declaration");
    Reject("#if __has_cpp_attribute(pga::Type)\nstruct [[pga::Type]] A {};\n#endif", "unsupported_condition");
}
TEST_F(MetaAttr, ModelSurvivesAstContext)
{
    auto result = Fixture("OptionsCases.h");
    ASSERT_TRUE(result) << Errors(result);
    std::filesystem::remove(Root / "Input.h");
    const auto model = std::move(result->Model);
    ASSERT_EQ(model.Types.size(), 1);
    EXPECT_EQ(model.Types.at(0).QualifiedName, "sample::Settings");
    EXPECT_EQ(model.Types.at(0).Fields.at(2).Key, "别名\"\n");
    EXPECT_TRUE(model.Types.at(0).Fields.at(2).ReadOnly);
    EXPECT_EQ(model.Types.at(0).Fields.at(0).CppType, "int");
    EXPECT_GT(model.Types.at(0).Fields.at(0).Source.Line, 0);
}
TEST_F(MetaAttr, ConstantsAndSpellingFailures)
{
    for (const auto* spelling : {"pga::type", "pga::Filed", "wrong::Type", "Type", "PGA::Type"})
    {
        const auto result = Parse("struct [[" + std::string(spelling) + "]] A {};");
        EXPECT_FALSE(result) << spelling;
    }
    Reject("struct [[pga::Type]] A { [[pga::Field(pg::meta::FieldOptions{.Key=\"\"})]] int Value; };",
           "invalid_constant");
    Reject(R"(struct [[pga::Type]] A { [[pga::Field(pg::meta::FieldOptions{.Key="a\0b"})]] int Value; };)",
           "invalid_constant");
    Reject(R"(struct [[pga::Type]] A { [[pga::Field(pg::meta::FieldOptions{.Key="\xff"})]] int Value; };)",
           "invalid_constant");
    Reject("pg::meta::FieldOptions Runtime(); struct [[pga::Type]] A { [[pga::Field(Runtime())]] int Value; };",
           "invalid_constant");
    Reject("pg::meta::FieldOptions options{}; struct [[pga::Type]] A { [[pga::Field(options)]] int Value; };",
           "invalid_constant");
    Reject("const pg::meta::FieldOptions kOptions{.Key=\"folded\"}; struct [[pga::Type]] A { [[pga::Field(kOptions)]] "
           "int Value; };",
           "invalid_constant");
    const auto constantIntegral = Parse("const int kReadOnly = 1; struct [[pga::Type]] A { "
                                        "[[pga::Field(pg::meta::FieldOptions{.ReadOnly=kReadOnly})]] int Value; };");
    ASSERT_TRUE(constantIntegral) << Errors(constantIntegral);
    Reject("struct FieldOptions { const char* Key; bool ReadOnly; }; struct [[pga::Type]] A { "
           "[[pga::Field(FieldOptions{})]] int Value; };",
           "invalid_options");
    Reject("struct [[pga::Type]] A { [[pga::Field(pg::meta::FieldOptions{.Key=\"same\"})]] int X; "
           "[[pga::Field(pg::meta::FieldOptions{.Key=\"same\"})]] int Y; };",
           "duplicate_field_key");
    Reject("struct [[pga::Type]] A { [[pga::Field,pga::Field]] int X; };", "unsupported_declaration");
    Reject("struct [[pga::Type(\"00000000000000000000000000000000\")]] A {};", "invalid_type_id");
    Reject("struct [[pga::Type(\"ABCDEF00000000000000000000000000\")]] A {};", "invalid_type_id");
    const auto tooMany = Parse(R"(struct [[pga::Type("a","b")]] A {};)");
    EXPECT_FALSE(tooMany);
    const auto unknown = Parse("struct [[pga::Type]] A { MissingType Value; };");
    ASSERT_FALSE(unknown);
    EXPECT_TRUE(std::ranges::any_of(unknown.error(), [](const auto& error)
                                    { return error.Message.contains("MissingType") && error.Source.Line != 0; }));
}
TEST_F(MetaAttr, UnsupportedDeclarations)
{
    Reject("namespace { namespace nested { struct [[pga::Type]] Hidden {}; } }", "unsupported_declaration");
    const auto statement = Parse("void F() { [[pga::Type]] if (true) {} }");
    EXPECT_FALSE(statement);
    for (const auto* kind : {"BITFIELD", "REFERENCE", "STATIC", "UNION", "TEMPLATE", "PRIVATE_TYPE", "ANONYMOUS"})
    {
        Reject("#define PG_TEST_" + std::string(kind) + "\n" + FixtureText("UnsupportedCases.h"),
               "unsupported_declaration");
    }
    Reject("void Function() { struct [[pga::Type]] Local {}; }", "unsupported_declaration");
    Reject("[[pga::Type]] void Function();", "unsupported_declaration");
    Reject("struct [[pga::Type]] Incomplete;", "unsupported_declaration");
    const auto result = Parse("struct [[pga::Type]] Outer { [[pga::Field]] struct { int Value; } Nested; };");
    ASSERT_TRUE(result) << Errors(result);
    EXPECT_EQ(result->Model.Types.at(0).Fields.at(0).CppType, "decltype(Outer::Nested)");
}
TEST_F(MetaAttr, DependenciesAndOtherModules)
{
    std::ofstream(Root / "Dependency.h") << FixtureText("Dependency.h");
    Request.Ownership.push_back({.Path = Root / "Dependency.h", .Target = "Other"});
    std::ofstream(Root / "Forced.h") << "#pragma once\n";
    std::ofstream(Root / "nested.rsp") << "-std=c++23 -I. -include Forced.h";
    std::ofstream(Root / "args.rsp") << "@nested.rsp -c Context.cpp -o Context.obj";
    Database({PG_TEST_COMPILER, "@args.rsp"});
    const auto result = Parse("#include \"Dependency.h\"\nstruct [[pga::Type]] Mine {};\n");
    ASSERT_TRUE(result) << Errors(result);
    ASSERT_EQ(result->Model.Types.size(), 1);
    EXPECT_EQ(result->Model.Types.at(0).QualifiedName, "Mine");
    for (const auto& path : {
             Root / "Input.h",
             Root / "Dependency.h",
             Root / "Forced.h",
             Root / "args.rsp",
             Root / "nested.rsp",
             Root / "CMakeCache.txt",
             Request.Context.Database,
             Path(PG_TEST_ENGINE) / "Source/Runtime/Foundation/Public/MetaOptions.h",
             Path(PG_TEST_RESOURCE) / "include/stddef.h",
         })
    {
        EXPECT_NE(std::ranges::find(result->Dependencies, std::filesystem::canonical(path)), result->Dependencies.end())
            << Utf8(path);
    }
    std::ofstream(Root / "Unregistered.h") << FixtureText("Unregistered.h");
    Reject("#include \"Unregistered.h\"\n", "unregistered_input");
    std::ofstream(Root / "Business.cpp") << "struct [[pga::Type]] Business {};\n";
    Request.Ownership.push_back({.Path = Root / "Business.cpp", .Target = "Sample"});
    Reject("#include \"Business.cpp\"\n", "unsupported_declaration");
}
TEST_F(MetaAttr, FrontendConditionsAndPragmas)
{
    Reject("#if 0\n#elifdef __clang__\nstruct [[pga::Type]] A {};\n#endif", "unsupported_condition");
    Reject("#if 0\n#elifndef __clang__\nstruct [[pga::Type]] A {};\n#else\nstruct [[pga::Type]] B {};\n#endif",
           "unsupported_condition");
    const auto unrelatedProbe = Parse(
        "#if __has_cpp_attribute(pga::Type)\nconstexpr bool kUnrelated = true;\n#endif\nstruct [[pga::Type]] A {};\n");
    ASSERT_TRUE(unrelatedProbe) << Errors(unrelatedProbe);
    Reject("#define META [[pga::Typo]]\n#pragma clang diagnostic ignored \"-Wunknown-attributes\"\nstruct META A {};",
           "unsupported_condition");
    std::ofstream(Root / "System.h")
        << "#pragma clang system_header\n#pragma clang diagnostic push\n#pragma clang diagnostic ignored "
           "\"-Wunknown-attributes\"\nstruct [[vendor::Allowed]] System {};\n#pragma clang diagnostic pop\n";
    const auto system = Parse("#include \"System.h\"\nstruct [[pga::Type]] A {};\n");
    ASSERT_TRUE(system) << Errors(system);
    Reject("constexpr pg::meta::FieldOptions Make() { return {.ReadOnly=(__clang_major__ > 1)}; } struct [[pga::Type]] "
           "A { [[pga::Field(Make())]] int X; };",
           "unsupported_condition");
    Reject("constexpr pg::meta::FieldOptions kOptions{.ReadOnly=(__has_cpp_attribute(pga::Type) != 0)}; struct "
           "[[pga::Type]] A { [[pga::Field(kOptions)]] int X; };",
           "unsupported_condition");
    const auto platformValue = Parse("constexpr pg::meta::FieldOptions Make() { return {.ReadOnly=(sizeof(void*) == "
                                     "8)}; } struct [[pga::Type]] A { [[pga::Field(Make())]] int X; };");
    ASSERT_TRUE(platformValue) << Errors(platformValue);
    Reject("#define PG_TEST_FRONTEND\n" + FixtureText("ConditionCases.h"), "unsupported_condition");
    const auto platform = Parse("#define PG_TEST_PLATFORM\n" + FixtureText("ConditionCases.h"));
    ASSERT_TRUE(platform) << Errors(platform);
    Reject("#pragma clang diagnostic ignored \"-Wunknown-attributes\"\nstruct [[pga::Typo]] A {};",
           "unsupported_condition");
    Reject("#pragma clang diagnostic ignored \"-Wattributes\"\nstruct [[pga::Type]] A {};", "unsupported_condition");
    Reject("#if __clang_major__ > 999\nstruct [[pga::Type]] A {};\n#else\nstruct [[pga::Type]] B {};\n#endif",
           "unsupported_condition");
    Reject("#if __clang_major__ > 1\nconstexpr auto kOptions=pg::meta::FieldOptions{};\n#else\nconstexpr auto "
           "kOptions=pg::meta::FieldOptions{.ReadOnly=true};\n#endif\nstruct [[pga::Type]] A { "
           "[[pga::Field(kOptions)]] int X; };",
           "unsupported_condition");
    Reject("#define FRONTEND_VERSION __clang_major__\n#if FRONTEND_VERSION > 1\nstruct [[pga::Type]] A {};\n#endif",
           "unsupported_condition");
    Reject("struct [[pga::Type]] A { [[pga::Field(pg::meta::FieldOptions{.ReadOnly=(__clang_major__ > 1)})]] int X; };",
           "unsupported_condition");
    Reject("#if __has_cpp_attribute(Type)\nstruct [[pga::Type]] A {};\n#endif", "unsupported_condition");
    const auto unrelated = Parse("#if __clang_major__ > 1\n#define UNRELATED 1\n#endif\nstruct [[pga::Type]] A {};\n");
    ASSERT_TRUE(unrelated) << Errors(unrelated);
    const auto harmless = Parse("#pragma clang diagnostic ignored \"-Wunused-variable\"\nstruct [[pga::Type]] A {};\n");
    ASSERT_TRUE(harmless) << Errors(harmless);
}
TEST_F(MetaAttr, CrossTuMergeAndPrivateHeader)
{
    const auto initial = Parse("#pragma once\nstruct [[pga::Type]] Shared { [[pga::Field]] int Value; };\n");
    ASSERT_TRUE(initial) << Errors(initial);
    std::ofstream(Root / "Second.h") << "#include \"Input.h\"\nstruct [[pga::Type]] Second {};\n";
    Request.Headers.push_back({
        .Path = Root / "Second.h",
        .Root = Root,
        .Prefix = {},
        .Visibility = HeaderVisibility::kPrivate,
        .TypedAccess = false,
        .Description = true,
    });
    Request.Ownership.push_back({.Path = Root / "Second.h", .Target = "Sample"});
    Request.Outputs.emplace_back("Private/Second.gen.h");
    Request.Outputs.emplace_back("Private/Second.gen.cpp");
    const auto result = Scan(Request);
    ASSERT_TRUE(result) << Errors(result);
    ASSERT_EQ(result->Model.Types.size(), 2);
    EXPECT_EQ(result->Model.Types.back().QualifiedName, "Shared");
    EXPECT_EQ(result->Model.Types.back().Fields.front().Key, "Value");
}
TEST_F(MetaAttr, IdentityDomainsByOwner)
{
    constexpr std::string_view kId = "00000000000000000000000000000001";
    const auto type = [&](std::string_view name)
    { return "struct [[pga::Type(\"" + std::string(kId) + "\")]] " + std::string(name) + " {};\n"; };
    Request.Ownership.push_back({.Path = Root / "Foreign.h", .Target = "Other"});
    std::ofstream(Root / "Foreign.h") << type("Foreign");
    const auto separate = Parse("#include \"Foreign.h\"\n" + type("Current"));
    EXPECT_TRUE(separate) << Errors(separate);
    if (separate)
    {
        ASSERT_EQ(separate->Model.Types.size(), 1);
        EXPECT_EQ(separate->Model.Types.front().QualifiedName, "Current");
    }
    Reject(type("Current") + type("Conflict"), "duplicate_type_id");
    std::ofstream(Root / "Foreign.h") << type("Foreign") << type("ForeignConflict");
    Reject("#include \"Foreign.h\"\nstruct [[pga::Type]] Current {};", "duplicate_type_id");
}
TEST_F(MetaAttr, ConditionalIncludeAndMacroOrigins)
{
    Reject("#define CAT(a,b) a##b\n#define REFLECT [[pga::Type]]\n#if __clang_major__ < 0\nstruct CAT(REF,LECT) A "
           "{};\n#endif",
           "unsupported_condition");
    Reject("#define ATTR(name) [[name]]\n#define REFLECT ATTR(pga::Type)\n#if __clang_major__ < 0\nstruct REFLECT A "
           "{};\n#endif",
           "unsupported_condition");
    Reject("#define ATTR(name) [[name]]\n#if __clang_major__ < 0\nstruct ATTR(pga::Type) A {};\n#endif",
           "unsupported_condition");
    const auto plainAttribute =
        Parse("#define ATTR(name) [[name]]\n#if __clang_major__ >= 1\nstruct ATTR(nodiscard) Plain {};\n#endif");
    EXPECT_TRUE(plainAttribute) << Errors(plainAttribute);
    std::ofstream(Root / "Dependency.h") << "struct [[pga::Type]] Dependency {};\n";
    std::ofstream(Root / "Bridge.h") << "#include \"Dependency.h\"\n";
    Request.Ownership.push_back({.Path = Root / "Dependency.h", .Target = "Other"});
    for (const auto* condition : {"__clang_major__ >= 1", "__clang_major__ < 0"})
    {
        Reject("#if " + std::string(condition) + "\n#include \"Dependency.h\"\n#endif", "unsupported_condition");
        Reject("#if " + std::string(condition) + "\n#include \"Bridge.h\"\n#endif", "unsupported_condition");
        Reject("#define REFLECT [[pga::Type]]\n#if " + std::string(condition) + "\nstruct REFLECT A {};\n#endif",
               "unsupported_condition");
        Reject("#define REFLECT [[pga::Type]]\n#define DECLARE(name) struct REFLECT name {};\n#if " +
                   std::string(condition) + "\nDECLARE(A)\n#endif",
               "unsupported_condition");
    }
    std::ofstream(Root / "Markers.h") << "#define REFLECT [[pga::Type]]\n";
    Reject("#if __clang_major__ >= 1\n#include \"Markers.h\"\n#endif\nstruct REFLECT A {};", "unsupported_condition");
    const auto platform = Parse("#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)\n#include "
                                "\"Bridge.h\"\n#endif\nstruct [[pga::Type]] Current {};");
    EXPECT_TRUE(platform) << Errors(platform);
    std::ofstream(Root / "Plain.h") << "constexpr int kUnrelated = 1;\n";
    const auto unrelated =
        Parse("#if __clang_major__ >= 1\n#include \"Plain.h\"\n#endif\nstruct [[pga::Type]] Current {};");
    EXPECT_TRUE(unrelated) << Errors(unrelated);
}
TEST_F(MetaAttr, ResolvedFactoryDependencies)
{
    Reject("struct Factory { bool Flag = (__clang_major__ > 1); constexpr Factory() = default; constexpr "
           "pg::meta::FieldOptions Make() const { return {.ReadOnly=Flag}; } }; struct [[pga::Type]] A { "
           "[[pga::Field(Factory{}.Make())]] int X; };",
           "unsupported_condition");
    Reject("struct Factory { constexpr pg::meta::FieldOptions Make(bool flag=(__clang_major__ > 1)) const { return "
           "{.ReadOnly=flag}; } }; constexpr Factory kFactory{}; struct [[pga::Type]] A { "
           "[[pga::Field(kFactory.Make())]] int X; };",
           "unsupported_condition");
    Reject("constexpr pg::meta::FieldOptions Make(){ return {}; } constexpr auto kMake = &Make; struct [[pga::Type]] A "
           "{ [[pga::Field(kMake())]] int X; };",
           "unsupported_condition");
    Reject("struct Factory { constexpr pg::meta::FieldOptions Make() const { return {.ReadOnly=(__clang_major__ > 1)}; "
           "} }; constexpr Factory kFactory{}; struct [[pga::Type]] A { [[pga::Field(kFactory.Make())]] int X; };",
           "unsupported_condition");
    Reject("struct Factory { constexpr pg::meta::FieldOptions Make() const { return "
           "{.ReadOnly=(__has_cpp_attribute(pga::Type) != 0)}; } }; constexpr Factory kFactory{}; struct [[pga::Type]] "
           "A { [[pga::Field(kFactory.Make())]] int X; };",
           "unsupported_condition");
    Reject("struct Factory { bool Flag; constexpr Factory() : Flag(__clang_major__ > 1) {} constexpr "
           "pg::meta::FieldOptions Make() const { return {.ReadOnly=Flag}; } }; constexpr Factory kFactory{}; struct "
           "[[pga::Type]] A { [[pga::Field(kFactory.Make())]] int X; };",
           "unsupported_condition");
    Reject("struct Factory { bool Flag = (__clang_major__ > 1); constexpr pg::meta::FieldOptions Make() const { return "
           "{.ReadOnly=Flag}; } }; constexpr Factory kFactory{}; struct [[pga::Type]] A { "
           "[[pga::Field(kFactory.Make())]] int X; };",
           "unsupported_condition");
    const auto platform =
        Parse("struct Factory { bool Flag; constexpr Factory() : Flag(sizeof(void*) == 8) {} constexpr "
              "pg::meta::FieldOptions Make() const { return {.ReadOnly=Flag}; } }; constexpr Factory kFactory{}; "
              "struct [[pga::Type]] A { [[pga::Field(kFactory.Make())]] int X; };");
    EXPECT_TRUE(platform) << Errors(platform);
    const auto defaults = Parse("struct Factory { bool Flag = sizeof(void*) == 8; constexpr pg::meta::FieldOptions "
                                "Make() const { return {.ReadOnly=Flag}; } }; constexpr Factory kFactory{}; struct "
                                "[[pga::Type]] A { [[pga::Field(kFactory.Make())]] int X; };");
    EXPECT_TRUE(defaults) << Errors(defaults);
    const auto defaulted = Parse("struct Factory { bool Flag = sizeof(void*) == 8; constexpr Factory() = default; "
                                 "constexpr pg::meta::FieldOptions Make() const { return {.ReadOnly=Flag}; } }; struct "
                                 "[[pga::Type]] A { [[pga::Field(Factory{}.Make())]] int X; };");
    EXPECT_TRUE(defaulted) << Errors(defaulted);
}
TEST_F(MetaAttr, ConstantStringStorage)
{
    Reject("char key[]=\"runtime\"; constexpr pg::meta::FieldOptions kOptions{.Key=key}; struct [[pga::Type]] A { "
           "[[pga::Field(kOptions)]] int X; };",
           "invalid_constant");
    Reject("char id[]=\"00000000000000000000000000000001\"; struct [[pga::Type(id)]] A {};", "invalid_type_id");
    const auto result =
        Parse("constexpr char kKey[]=\"prefix_key\"; constexpr char kId[]=\"prefix_00000000000000000000000000000001\"; "
              "struct [[pga::Type(kId+7)]] A { [[pga::Field(pg::meta::FieldOptions{.Key=kKey+7})]] int X; };");
    ASSERT_TRUE(result) << Errors(result);
    EXPECT_EQ(result->Model.Types.front().Fields.front().Key, "key");
    EXPECT_TRUE(result->Model.Types.front().Id);
}
