#include "GameplayModule.h"
#include "PhysicsModule.h"
#include <FixtureModules.h>

#include <FoundationModule.h>
#include <ModuleBuiltin.h>
#include <ModuleDescriptor.h>
#include <ModuleManager.h>
#include <RegistrationContext.h>

#include <gtest/gtest.h>

#include <array>

TEST(ProjectPluginCollaboration, RegistersProviderFirst)
{
    const std::array queries{
        pg::module::QueryFoundationModule,
        pg::module::QueryModuleRuntime,
        pg::fixture::QueryGameplay,
        pg::fixture::QueryPhysics,
    };
    pg::module::RegistrationScope scope;
    pg::module::ModuleOperationReport report;
    auto host = pg::module::ModuleManager::Create(scope, queries, {}, report);
    ASSERT_TRUE(host);
    ASSERT_EQ(host->Modules().size(), 4U);
    const auto order = host->RegistrationOrder();
    ASSERT_EQ(order.size(), 4U);
    EXPECT_EQ(host->Inspect(order.at(2))->Descriptor.Id, pg::fixture::kPhysicsId);
    EXPECT_EQ(host->Inspect(order.back())->Descriptor.Id, pg::fixture::kGameplayId);
    for (const auto id : order)
    {
        ASSERT_TRUE(host->Register(id, report));
        ASSERT_TRUE(host->Activate(id, report));
    }
    EXPECT_EQ(scope.Declarations().size(), 2U);
}
