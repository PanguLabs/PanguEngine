#pragma once
#if defined(PG_TEST_BITFIELD)
struct [[pga::Type]] Bad
{
    [[pga::Field]] unsigned Value : 1;
};
#elif defined(PG_TEST_REFERENCE)
struct [[pga::Type]] Bad
{
    [[pga::Field]] int& Value;
};
#elif defined(PG_TEST_STATIC)
struct [[pga::Type]] Bad
{
    [[pga::Field]] static int Value;
};
#elif defined(PG_TEST_UNION)
union [[pga::Type]] Bad
{
    int Value;
};
#elif defined(PG_TEST_TEMPLATE)
template <class T>
struct [[pga::Type]] Bad
{
    T Value;
};
#elif defined(PG_TEST_PRIVATE_TYPE)
class Outer
{
    struct [[pga::Type]] Bad
    {
    };
};
#elif defined(PG_TEST_ANONYMOUS)
namespace
{
struct [[pga::Type]] Bad
{
};
} // namespace
#endif
