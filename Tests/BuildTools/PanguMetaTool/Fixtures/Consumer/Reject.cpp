#include <Actor.gen.h>
#include <MetaTraits.h>
#include <tuple>
void Reject()
{
    pg::test::Actor actor;
#if PG_TEST_REJECT == 1
    std::get<1>(pg::meta::TypeTraits<pg::test::Actor>::Fields).Write(actor, 3);
#elif PG_TEST_REJECT == 2
    std::get<2>(pg::meta::TypeTraits<pg::test::Actor>::Fields).Write(actor, 3);
#elif PG_TEST_REJECT == 3
    pg::meta::FieldAccess<&pg::test::Actor::Secret> field{};
    (void)field;
    (void)actor;
#else
    const pg::test::Actor& fixed = actor;
    std::get<0>(pg::meta::TypeTraits<pg::test::Actor>::Fields).Write(fixed, 2.0F);
#endif
}
