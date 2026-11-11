#pragma once
#include <MetaOptions.h>
#include <cstdint>

namespace pg::test::meta_fixture
{
enum class Mood : std::uint8_t
{
    kIdle,
    kMoving,
};
struct Position
{
    int X{};
    int Y{};
};

// 公共值类型不导入业务 DLL；POSIX 跨库 RTTI 使用默认可见性。
#ifndef _WIN32
#pragma GCC visibility push(default)
#endif
class [[pga::Type]] Actor
{
public:
    // 夹具有意包含可访问数据及 private 仅描述字段。
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    [[pga::Field]] bool Enabled{};
    [[pga::Field(pg::meta::FieldOptions{.Key = "health"})]] int Health{10};
    [[pga::Field]] float Speed{1.0F};
    [[pga::Field]] Mood State{Mood::kIdle};
    [[pga::Field]] Position Location{};
    // const 事实与属性政策是独立契约。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    [[pga::Field]] const int Fixed{7};
    [[pga::Field(pg::meta::FieldOptions{.ReadOnly = true})]] int Locked{8};
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
    [[nodiscard]] int GetSecret() const noexcept { return Secret; }
    void SetSecret(int value) noexcept { Secret = value; }

private:
    [[pga::Field]] int Secret{9};
};
struct [[pga::Type("e2000000000000000000000000000001")]] StableActor
{
    [[pga::Field]] int Value{11};
};
#ifndef _WIN32
#pragma GCC visibility pop
#endif
} // namespace pg::test::meta_fixture
