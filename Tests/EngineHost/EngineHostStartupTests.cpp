#include "HostOperation.h"
#include "ModuleManager.h"
#include <EngineHost.h>
#include <EngineHostModule.h>
#include <FoundationModule.h>
#include <HostStartupConfig.h>
#include <ModuleBuiltin.h>
#include <RegistrationContext.h>
#include <array>
#include <gtest/gtest.h>
#include <type_traits>

#ifdef PG_TEST_MODULAR
#include <ScopeTestSupport.h>
#include <SharedFixtureContract.h>
#include <SharedFixturePaths.h>
#endif

namespace pg::engine
{
static_assert(!std::is_copy_constructible_v<EngineHost>);
static_assert(!std::is_copy_assignable_v<EngineHost>);
static_assert(!std::is_move_constructible_v<EngineHost>);
static_assert(!std::is_move_assignable_v<EngineHost>);

TEST(EngineHostStartup, ColdConstructionHasNoModuleSideEffects)
{
    const EngineHost host;
    EXPECT_EQ(host.GetState(), HostState::kCold);
    EXPECT_FALSE(host.HostScope().IsValid());
    const auto modules = host.InspectModules();
    ASSERT_TRUE(modules);
    EXPECT_TRUE(modules->empty());
    const auto declarations = host.InspectDeclarations();
    ASSERT_TRUE(declarations);
    EXPECT_TRUE(declarations->empty());
}

TEST(EngineHostStartup, EmptyBusinessStartsThreeResidents)
{
    EngineHost host;
    HostOperationReport report;
    ASSERT_TRUE(host.Start({}, report));
    EXPECT_EQ(host.GetState(), HostState::kReady);
    EXPECT_EQ(report.State, HostState::kReady);
    EXPECT_FALSE(report.PrimaryError);
    EXPECT_TRUE(host.HostScope().IsValid());
    EXPECT_EQ(module::BuiltinResidentQueries().size(), 2U);
    const auto modules = host.InspectModules();
    ASSERT_TRUE(modules);
    ASSERT_EQ(modules->size(), 3U);
    EXPECT_EQ(modules->at(0).Descriptor.Id, module::kFoundationModuleId);
    EXPECT_EQ(modules->at(1).Descriptor.Id, module::kModuleRuntimeModuleId);
    EXPECT_EQ(modules->at(2).Descriptor.Id, kEngineHostModuleId);
    for (const auto& snapshot : *modules)
    {
        EXPECT_TRUE(snapshot.Resident);
        EXPECT_EQ(snapshot.State, module::ModuleState::kActive);
    }
    ASSERT_TRUE(host.BeginShutdown(report));
    EXPECT_EQ(host.GetState(), HostState::kClosing);
    ASSERT_TRUE(host.TryFinishShutdown(report));
    EXPECT_EQ(host.GetState(), HostState::kClosed);
    EXPECT_FALSE(host.HostScope().IsValid());
    ASSERT_TRUE(host.InspectModules());
    EXPECT_TRUE(host.InspectModules()->empty());
    ASSERT_TRUE(host.InspectDeclarations());
    EXPECT_TRUE(host.InspectDeclarations()->empty());
}

#ifndef PG_TEST_MODULAR
namespace
{
constexpr module::ModuleId kMetadataModule{.Bytes = {std::byte{0xe1}}};
constexpr module::DeclarationId kMetadataDeclaration{.Bytes = {std::byte{0xe2}}};
Status RegisterMetadata(module::RegistrationContext& context) noexcept
{
    return context.Declare(kMetadataDeclaration, "metadata-only");
}
const module::ModuleDefinition* QueryMetadata() noexcept
{
    static constexpr module::ModuleDefinition kDefinition{
        .Descriptor = {.Id = kMetadataModule, .Dependencies = {}, .DisplayName = "metadata-only", .BuildIdentity = {}},
        .RegisterModule = RegisterMetadata};
    return &kDefinition;
}
} // namespace
#endif

TEST(EngineHostStartup, ServiceLessBusinessParticipatesNormally)
{
#ifdef PG_TEST_MODULAR
    pg::test::Observer().Reset();
    const auto id = module::fixture::kSharedProviderId;
    const auto declaration = module::fixture::kSharedProviderDeclaration;
    const std::array candidates{
        module::ModuleLoadCandidate{
            module::SharedModuleCandidate{.ExpectedId = id, .Path = module::fixture::kSharedProviderPath}},
    };
#else
    const auto id = kMetadataModule;
    const auto declaration = kMetadataDeclaration;
    const std::array candidates{module::ModuleLoadCandidate{module::StaticModuleCandidate{QueryMetadata}}};
#endif
    EngineHost host;
    HostOperationReport report;
    HostStartupConfig config;
    config.Modules = candidates;
    ASSERT_TRUE(host.Start(config, report));
    EXPECT_EQ(host.GetState(), HostState::kReady);
    const auto modules = host.InspectModules();
    ASSERT_TRUE(modules);
    ASSERT_EQ(modules->size(), 4U);
    EXPECT_EQ(modules->back().Descriptor.Id, id);
    EXPECT_FALSE(modules->back().Resident);
    EXPECT_EQ(modules->back().State, module::ModuleState::kActive);
    const auto declarations = host.InspectDeclarations();
    ASSERT_TRUE(declarations);
    ASSERT_EQ(declarations->size(), 1U);
    EXPECT_EQ(declarations->front().Owner, id);
    EXPECT_EQ(declarations->front().Id, declaration);
    ASSERT_TRUE(host.BeginShutdown(report));
    ASSERT_TRUE(host.TryFinishShutdown(report));
}
} // namespace pg::engine
