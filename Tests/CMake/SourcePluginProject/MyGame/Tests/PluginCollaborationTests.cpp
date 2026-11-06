#include <FixtureModules.h>

#include <ModuleDescriptor.h>
#include <RegistrationContext.h>
#include <StaticModuleHost.h>

#include <gtest/gtest.h>

#include <array>

TEST(ProjectPluginCollaboration, RegistersProviderFirst)
{
    const std::array queries{
        pg::module::StaticCatalogEntry{pg::fixture::QueryGameplay},
        pg::module::StaticCatalogEntry{pg::fixture::QueryPhysics},
    };
    pg::module::RegistrationScope scope;
    pg::module::ModuleOperationReport report;
    auto host = pg::module::StaticModuleHost::Create({queries}, scope, {}, report);
    ASSERT_TRUE(host);
    ASSERT_EQ(host->Modules().size(), 2U);
    const auto order = host->RegistrationOrder();
    ASSERT_EQ(order.size(), 2U);
    EXPECT_EQ(order.front(), pg::fixture::kPhysicsId);
    EXPECT_EQ(order.back(), pg::fixture::kGameplayId);
    for (const auto id : order)
    {
        ASSERT_TRUE(host->Register(id, report));
    }
    EXPECT_EQ(scope.Declarations().size(), 2U);
}
