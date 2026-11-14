#include "GameplayModule.h"
#include <FixtureModules.h>

#include <gtest/gtest.h>

TEST(GameplayPlugin, DependsOnPhysics)
{
    const auto* definition = pg::fixture::QueryGameplay();
    ASSERT_NE(definition, nullptr);
    EXPECT_EQ(definition->Descriptor.Id, pg::fixture::kGameplayId);
    ASSERT_EQ(definition->Descriptor.Dependencies.size(), 1U);
    EXPECT_EQ(definition->Descriptor.Dependencies.front().Id, pg::fixture::kPhysicsId);
}
