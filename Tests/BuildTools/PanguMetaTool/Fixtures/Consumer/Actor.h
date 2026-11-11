#pragma once
#include <MetaOptions.h>
static_assert(PG_TEST_RSP == 61);
namespace pg::test
{
class [[pga::Type("e9000000000000000000000000000009")]] Actor
{
public:
    // 夹具刻意同时覆盖 public 访问与 private 仅描述边界。
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    [[pga::Field]] float Speed{1.0F};
    // 语言 const 必须与 ReadOnly 属性政策分别验证。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    [[pga::Field]] const int Fixed{7};
    [[pga::Field(pg::meta::FieldOptions{.ReadOnly = true})]] int Locked{8};
    [[pga::Field(pg::meta::FieldOptions{.Key = "quote\"\\\n\001F中"})]] int Escaped{9};
    [[pga::Field]] struct
    {
        int Value{};
    } Anonymous;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
    [[nodiscard]] int GetSecret() const noexcept { return Secret; }

private:
    [[pga::Field]] int Secret{10};
};
} // namespace pg::test

// 生成的 Traits 处于 pg::meta；此同名命名空间迫使其引用使用全局限定。
namespace pg::meta::pg
{
}
