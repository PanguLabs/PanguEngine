#include "FixtureModules.h"

#include <ModuleDescriptor.h>
#include <RegistrationContext.h>
#include <ScopeIdentity.h>
#include <StaticModuleHost.h>

#include <gtest/gtest.h>

TEST(StaticCatalog, RetainsArchiveRootsAndRunsRealFactories)
{
    using namespace pg::module;
    RegistrationScope scope;
    ModuleOperationReport report;
    const auto catalog = fixture::GeneratedCatalog();
    auto host = StaticModuleHost::Create(catalog, scope, {}, report);
    ASSERT_TRUE(host);
    ASSERT_EQ(host->Modules().size(), 2U);
    ASSERT_EQ(catalog.Entries.size(), 2U);
    ASSERT_EQ(host->RegistrationOrder().size(), 2U);
    EXPECT_EQ(host->RegistrationOrder().front(), fixture::kSecondId);
    EXPECT_EQ(host->RegistrationOrder().back(), fixture::kFirstId);
    for (const auto id : host->RegistrationOrder())
    {
        ASSERT_TRUE(host->Register(id, report));
    }
    const auto declarations = scope.Declarations();
    ASSERT_EQ(declarations.size(), 2U);
    EXPECT_EQ(declarations.front().Id, fixture::kSecondDeclaration);
    EXPECT_EQ(declarations.front().Owner, fixture::kSecondId);
    EXPECT_EQ(declarations.front().DisplayName, "second");
    EXPECT_EQ(declarations.back().Id, fixture::kFirstDeclaration);
    EXPECT_EQ(declarations.back().Owner, fixture::kFirstId);
    EXPECT_EQ(declarations.back().DisplayName, "first");
    for (const auto id : host->RegistrationOrder())
    {
        ASSERT_TRUE(host->Activate(id, report));
    }
    const auto first = host->CreateScope(pg::service::Scope::kContext, host->HostScope(), report);
    const auto second = host->CreateScope(pg::service::Scope::kContext, host->HostScope(), report);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_NE(*first, *second);
    ASSERT_TRUE(host->BeginCloseScope(*first, report));
    ASSERT_TRUE(host->TryFinishCloseScope(*first, report));
    ASSERT_TRUE(host->BeginShutdown(report));
    ASSERT_TRUE(host->TryFinishShutdown(report));
    EXPECT_TRUE(scope.Declarations().empty());
}