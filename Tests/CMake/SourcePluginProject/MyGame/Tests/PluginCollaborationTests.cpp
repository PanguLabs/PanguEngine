#include "GameplayModule.h"
#include "PhysicsModule.h"
#include <FixtureModules.h>
#include <LogModule.h>

#include <FoundationModule.h>
#include <ModuleBuiltin.h>
#include <ModuleManager.h>
#include <ModuleOperation.h>
#include <RegistrationContext.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

TEST(ProjectPluginCollaboration, RegistersProviderFirst)
{
    const std::array queries{
        pg::module::QueryFoundationModule, pg::log::QueryLogModule,   pg::module::QueryModuleRuntime,
        pg::fixture::QueryGameplay,        pg::fixture::QueryPhysics,
    };
    pg::module::RegistrationScope scope;
    pg::module::ModuleOperationReport report;
    auto host = pg::module::ModuleManager::Create(scope, queries, {}, report);
    ASSERT_TRUE(host);
    ASSERT_EQ(host->Modules().size(), 5U);
    const auto order = host->RegistrationOrder();
    ASSERT_EQ(order.size(), 5U);
    const std::array expected{
        pg::module::kFoundationModuleId, pg::log::kLogModuleId,    pg::module::kModuleRuntimeModuleId,
        pg::fixture::kPhysicsId,         pg::fixture::kGameplayId,
    };
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        const auto snapshot = host->Inspect(order.at(index));
        ASSERT_TRUE(snapshot);
        EXPECT_EQ(snapshot->Descriptor.Id, expected.at(index));
    }
    for (const auto id : order)
    {
        ASSERT_TRUE(host->Register(id, report));
        ASSERT_TRUE(host->Activate(id, report));
    }
    EXPECT_EQ(scope.Declarations().size(), 2U);
}
