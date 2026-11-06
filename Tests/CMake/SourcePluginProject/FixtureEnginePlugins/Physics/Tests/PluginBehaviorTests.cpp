#include <FixtureModules.h>

#include <gtest/gtest.h>

TEST(PhysicsPlugin, DefinitionMatchesModule)
{
    const auto* definition = pg::fixture::QueryPhysics();
    ASSERT_NE(definition, nullptr);
    EXPECT_EQ(definition->Descriptor.Id, pg::fixture::kPhysicsId);
}
