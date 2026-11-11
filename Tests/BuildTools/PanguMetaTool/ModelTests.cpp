#include "MetaIR/Model.h"
#include "MetaIR/Normalize.h"
#include "MetaIdentity.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <system_error>
#include <utility>

namespace
{
pg::meta::ir::TypeDefinition MakeType(std::string name)
{
    return {
        .QualifiedName = std::move(name),
        .Id = {},
        .Source = {.File = "Types.h", .Line = 4, .Column = 1},
        .Fields =
            {
                {.Name = "Z", .Key = {}, .CppType = "int", .Source = {.File = "Types.h", .Line = 6, .Column = 2}},
                {.Name = "A", .Key = "key", .CppType = "const int", .IsConst = true},
            },
    };
}
} // namespace

TEST(MetaIR, OptionalIdentityAndMerge)
{
    using namespace pg::meta::ir;
    const auto zero = ParseTypeId("00000000000000000000000000000000");
    ASSERT_FALSE(zero);
    EXPECT_EQ(zero.error().front().Code, "invalid_type_id");
    EXPECT_FALSE(ParseTypeId("xyz"));
    EXPECT_FALSE(ParseTypeId("00000000-0000-0000-0000-000000000001"));
    for (const auto* text : {"0123456789aBcDeF0123456789abcdef", "0123456789ABCDEF0123456789ABCDEF"})
    {
        const auto uppercase = ParseTypeId(text);
        EXPECT_FALSE(uppercase) << text;
        if (!uppercase)
        {
            EXPECT_EQ(uppercase.error().front().Code, "invalid_type_id");
        }
    }
    const auto id = ParseTypeId("0123456789abcdef0123456789abcdef");
    ASSERT_TRUE(id);
    auto type = MakeType("::game::Player");
    auto normalized = Normalize({.ContextId = {}, .Types = {type, type}});
    ASSERT_TRUE(normalized);
    ASSERT_EQ(normalized->Types.size(), 1);
    EXPECT_FALSE(normalized->Types.front().Id);
    EXPECT_EQ(normalized->Types.front().QualifiedName, "game::Player");
    EXPECT_EQ(normalized->Types.front().Fields.front().Key, "Z");
    EXPECT_TRUE(normalized->Types.front().Fields.back().IsConst);
    EXPECT_FALSE(normalized->Types.front().Fields.back().ReadOnly);
    auto conflict = type;
    conflict.Fields.front().CppType = "float";
    EXPECT_FALSE(Normalize({.ContextId = {}, .Types = {type, conflict}}));
    conflict = type;
    conflict.Source.Line = 40;
    EXPECT_FALSE(Normalize({.ContextId = {}, .Types = {type, conflict}}));
    type.Id = *id;
    conflict = MakeType("Other");
    conflict.Id = *id;
    auto duplicate = Normalize({.ContextId = {}, .Types = {type, conflict}});
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().front().Code, "duplicate_type_id");
    type.Id = pg::meta::TypeId{};
    EXPECT_FALSE(Normalize({.ContextId = {}, .Types = {type}}));
    type.Id.reset();
    type.Fields.back().Key = "Z";
    auto keys = Normalize({.ContextId = {}, .Types = {type}});
    ASSERT_FALSE(keys);
    EXPECT_EQ(keys.error().front().Code, "duplicate_field_key");
}

TEST(MetaIR, OrderIsDeterministic)
{
    using namespace pg::meta::ir;
    Model input{.ContextId = "Context", .Types = {MakeType("Z"), MakeType("A")}};
    auto first = Normalize(input);
    std::ranges::reverse(input.Types);
    auto second = Normalize(std::move(input));
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_EQ(first->Types.size(), 2);
    EXPECT_EQ(first->Types.front().QualifiedName, "A");
    EXPECT_EQ(second->Types.front().QualifiedName, "A");
    EXPECT_EQ(first->Types.front().Fields.front().Name, "Z");
    EXPECT_EQ(second->Types.front().Fields.back().Name, "A");
}

TEST(MetaIR, OwnsDataAfterInputDestruction)
{
    using namespace pg::meta::ir;
    Model copy;
    {
        auto type = MakeType(std::string(120, 'T'));
        type.Fields.back().Key = std::string(120, 'K');
        type.Source.File = std::string(120, 'F');
        type.Id = *ParseTypeId("0123456789abcdef0123456789abcdef");
        Model original{.ContextId = std::string(120, 'C'), .Types = {type}};
        copy = original;
        original.Types.front().Fields.back().Key = "changed";
    }
    auto moved = std::move(copy);
    auto result = Normalize(std::move(moved));
    ASSERT_TRUE(result);
    EXPECT_EQ(result->ContextId, std::string(120, 'C'));
    EXPECT_EQ(result->Types.front().Fields.back().Key, std::string(120, 'K'));
    EXPECT_EQ(result->Types.front().Source.File, std::string(120, 'F'));
    EXPECT_EQ(result->Types.front().Id, *ParseTypeId("0123456789abcdef0123456789abcdef"));
}

TEST(MetaIR, CanonicalFileAliases)
{
    const auto root = std::filesystem::current_path() / "ModelCanonicalAliases";
    std::filesystem::create_directories(root);
    const auto original = root / "Original.h";
    const auto alias = root / "Alias.h";
    std::ofstream(original) << "struct Example {};";
    std::error_code error;
    std::filesystem::create_hard_link(original, alias, error);
    ASSERT_FALSE(error);
    auto type = MakeType("Example");
    auto other = type;
    const auto originalUtf8 = original.generic_u8string();
    const auto aliasUtf8 = alias.generic_u8string();
    type.Source.File.assign(originalUtf8.begin(), originalUtf8.end());
    other.Source.File.assign(aliasUtf8.begin(), aliasUtf8.end());
    const auto result = pg::meta::ir::Normalize({.ContextId = {}, .Types = {type, other}});
    ASSERT_TRUE(result);
    EXPECT_EQ(result->Types.size(), 1);
    std::filesystem::remove_all(root, error);
    EXPECT_FALSE(error);
}
