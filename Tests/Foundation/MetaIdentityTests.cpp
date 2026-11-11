#include <MetaIdentity.h>
#include <MetaInfo.h>
#include <MetaOptions.h>
#include <ModuleIdentity.h>

#include <cstddef>
#include <gtest/gtest.h>
#include <optional>
#include <type_traits>

namespace
{
using pg::meta::TypeId;

static_assert(!TypeId{}.IsValid());
constexpr TypeId kId{.Bytes = {std::byte{1}}};
static_assert(kId.IsValid());
static_assert(TypeId{} < kId);
static_assert(!std::is_convertible_v<TypeId, pg::module::ModuleId>);
static_assert(!std::is_convertible_v<pg::module::ModuleId, TypeId>);

TEST(MetaIdentity, OptionalAndDistinct)
{
    EXPECT_FALSE(TypeId{}.IsValid());
    constexpr pg::meta::TypeInfo kAnonymous{.Name = "Actor", .Id = std::nullopt, .Fields = {}};
    EXPECT_FALSE(kAnonymous.Id.has_value());
    constexpr pg::meta::TypeInfo kStable{.Name = "Actor", .Id = kId, .Fields = {}};
    ASSERT_TRUE(kStable.Id.has_value());
    EXPECT_EQ(*kStable.Id, kId);
    EXPECT_TRUE(kStable.Id->IsValid());
    constexpr pg::meta::FieldOptions kOptions;
    EXPECT_EQ(kOptions.Key, nullptr);
    EXPECT_FALSE(kOptions.ReadOnly);
}
} // namespace
