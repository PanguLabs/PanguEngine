#include <MetaIdentity.h>
#include <MetaOptions.h>
#include <MetaTraits.h>
#include <ModuleIdentity.h>

#include <array>
#include <concepts>
#include <cstddef>
#include <gtest/gtest.h>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

namespace
{
struct Payload
{
    int Count{};
};

struct Actor
{
    int Health{7};
    // 验证语言级 const 成员不能通过字段访问器写入。
    const int Version{1}; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    float Speed{1.0F};
    // 原生数组契约必须保留数组类型，不可替换为 std::array。
    int Samples[2]{3, 4}; // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    Payload State{.Count = 5};
    volatile int Signal{6};
};

constexpr pg::meta::FieldAccess<&Actor::Health, true> kHealth{
    .Info =
        {
            .Name = "Health",
            .Key = "hp",
            .TypeName = "int",
            .Visibility = pg::meta::Access::kPublic,
            .IsConst = false,
            .IsReadOnly = true,
        },
};
constexpr pg::meta::FieldAccess<&Actor::Version> kVersion{
    .Info =
        {
            .Name = "Version",
            .Key = "Version",
            .TypeName = "const int",
            .Visibility = pg::meta::Access::kPublic,
            .IsConst = true,
            .IsReadOnly = false,
        },
};
constexpr pg::meta::FieldAccess<&Actor::Speed> kSpeed{
    .Info =
        {
            .Name = "Speed",
            .Key = "Speed",
            .TypeName = "float",
            .Visibility = pg::meta::Access::kPublic,
            .IsConst = false,
            .IsReadOnly = false,
        },
};
constexpr pg::meta::FieldAccess<&Actor::Samples> kSamples{};
constexpr pg::meta::FieldAccess<&Actor::State> kState{};
constexpr pg::meta::FieldAccess<&Actor::Signal> kSignal{};

template <class T>
concept CanWriteSpeed = requires(Actor& actor, T&& value) { kSpeed.Write(actor, std::forward<T>(value)); };

template <class Field, class Owner, class Value>
concept CanWrite = requires(const Field& field, Owner&& owner, Value&& value) {
    field.Write(std::forward<Owner>(owner), std::forward<Value>(value));
};

template <class Field, class Owner>
concept CanRead = requires(const Field& field, Owner&& owner) { field.Read(std::forward<Owner>(owner)); };

static_assert(!CanWriteSpeed<int>);
static_assert(CanWriteSpeed<float>);
static_assert(CanWriteSpeed<const float&>);
static_assert(!CanWrite<decltype(kSpeed), const Actor&, float>);
static_assert(!CanWrite<decltype(kSpeed), Actor, float>);
static_assert(!CanWrite<decltype(kHealth), Actor&, int>);
static_assert(!CanWrite<decltype(kVersion), Actor&, int>);
static_assert(!CanWrite<decltype(kSamples), Actor&, decltype(Actor::Samples)&>);
static_assert(!CanRead<decltype(kSpeed), Actor>);
static_assert(!CanRead<decltype(kSpeed), const Actor>);
static_assert(std::same_as<decltype(kSamples.Read(std::declval<const Actor&>())), const decltype(Actor::Samples)&>);
static_assert(std::same_as<decltype(kSamples)::ValueType, decltype(Actor::Samples)>);
static_assert(std::same_as<decltype(kVersion)::MemberType, const int>);
static_assert(std::same_as<decltype(kState)::ValueType, Payload>);
static_assert(std::same_as<decltype(kState.Read(std::declval<const Actor&>())), const Payload&>);
static_assert(std::same_as<decltype(kSignal.Read(std::declval<const Actor&>())), const volatile int&>);
static_assert(noexcept(kSpeed.Write(std::declval<Actor&>(), 2.0F)));
} // namespace

template <>
struct pg::meta::TypeTraits<Actor>
{
    // 以下成员名是生成特化的公共接口契约。
    // NOLINTBEGIN(readability-identifier-naming)
    static constexpr pg::module::ModuleId Owner{};
    static constexpr std::string_view Name{"Actor"};
    static constexpr std::optional<pg::meta::TypeId> Id{std::nullopt};
    static constexpr bool HasTypedAccess{true};
    static constexpr bool HasDescription{true};
    static constexpr auto Fields = std::tuple{kHealth, kVersion, kSpeed};
    // NOLINTEND(readability-identifier-naming)
};

namespace
{
struct DescriptionOnly
{
};
struct Unknown
{
};
} // namespace

template <>
struct pg::meta::TypeTraits<DescriptionOnly>
{
    // 生成特化的公共接口名称保持一致。
    static constexpr bool HasTypedAccess{false}; // NOLINT(readability-identifier-naming)
};

namespace
{
struct FieldVisitor
{
    template <class Field>
    void operator()(const Field&) const;
};

template <class T>
concept CanVisitFields = requires { pg::meta::ForEachField<T>(FieldVisitor{}); };
static_assert(CanVisitFields<Actor>);
static_assert(!CanVisitFields<DescriptionOnly>);
static_assert(!CanVisitFields<Unknown>);

TEST(MetaTraits, ExactWriteAndConstRead)
{
    Actor actor;
    EXPECT_EQ(kHealth.Read(actor), 7);
    EXPECT_EQ(kVersion.Read(actor), 1);
    kSpeed.Write(actor, 2.0F);
    EXPECT_FLOAT_EQ(actor.Speed, 2.0F);
    EXPECT_EQ(&kSpeed.Read(actor), &actor.Speed);
    EXPECT_EQ(kSamples.Read(actor)[1], 4);
    EXPECT_EQ(&kSamples.Read(actor), &actor.Samples);
    kState.Write(actor, Payload{.Count = 9});
    EXPECT_EQ(kState.Read(actor).Count, 9);
    kSignal.Write(actor, 8);
    EXPECT_EQ(kSignal.Read(actor), 8);
}

TEST(MetaTraits, VisitsFieldsInDeclarationOrder)
{
    EXPECT_FALSE(pg::meta::TypeTraits<Actor>::Owner.IsValid());
    EXPECT_EQ(pg::meta::TypeTraits<Actor>::Name, "Actor");
    EXPECT_FALSE(pg::meta::TypeTraits<Actor>::Id.has_value());
    EXPECT_TRUE(pg::meta::TypeTraits<Actor>::HasDescription);
    std::array<std::string_view, 3> names{};
    std::size_t index{};
    pg::meta::ForEachField<Actor>([&](const auto& field) { names.at(index++) = field.Info.Name; });
    EXPECT_EQ(index, names.size());
    EXPECT_EQ(names, (std::array<std::string_view, 3>{"Health", "Version", "Speed"}));
    EXPECT_FALSE(kHealth.Info.IsConst);
    EXPECT_TRUE(kHealth.Info.IsReadOnly);
    EXPECT_TRUE(kVersion.Info.IsConst);
    EXPECT_FALSE(kVersion.Info.IsReadOnly);
    EXPECT_EQ(kHealth.Info.Key, "hp");
}
} // namespace
