#include "Error.h"
#include "Fixtures/ScopeLifecycleFixture.h"
#include "ModuleDescriptor.h"
#include "ModuleIdentity.h"
#include "ModuleOperation.h"
#include "Result.h"
#include "ScopeIdentity.h"
#include <FoundationModule.h>
#include <ModuleBuiltin.h>
#include <ModuleError.h>
#include <ModuleManager.h>
#include <RegistrationContext.h>
#include <array>
#include <cstddef>
#include <expected>
#include <gtest/gtest.h>
#include <memory>

namespace pg::module
{
namespace
{
constexpr ModuleId kResident{.Bytes = {std::byte{0x55}}};
constexpr Error kFailure{ErrorDomainId::Constant<0x7461736b666f7572ULL>(), 1};
Status FailRegister(RegistrationContext& /*context*/) noexcept { return std::unexpected(kFailure); }
const ModuleDefinition* QueryFailure() noexcept
{
    static constexpr ModuleDefinition kDefinition{
        .Descriptor = {.Id = kResident, .Dependencies = {}, .DisplayName = {}, .BuildIdentity = {}},
        .RegisterModule = FailRegister};
    return &kDefinition;
}
struct ResidentFixture
{
    inline static std::shared_ptr<pg::test::scope_lifecycle::Counters> gState;
    static Status Register(RegistrationContext& context) noexcept
    {
        return pg::test::scope_lifecycle::RegisterObjects(context, gState);
    }
    static const ModuleDefinition* Query() noexcept
    {
        static constexpr ModuleDefinition kDefinition{
            .Descriptor = {.Id = kResident, .Dependencies = {}, .DisplayName = {}, .BuildIdentity = {}},
            .RegisterModule = Register};
        return &kDefinition;
    }
};
} // namespace
TEST(ResidentModule, ExplicitStartupAndShutdownConfirmation)
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    const auto handles = manager->RegistrationOrder();
    ASSERT_EQ(handles.size(), 2U);
    const auto foundation = manager->Inspect(handles.front());
    const auto runtime = manager->Inspect(handles.back());
    ASSERT_TRUE(foundation);
    ASSERT_TRUE(runtime);
    EXPECT_EQ(foundation->Descriptor.Id, kFoundationModuleId);
    EXPECT_EQ(runtime->Descriptor.Id, kModuleRuntimeModuleId);
    EXPECT_EQ(foundation->Descriptor.Version, (ModuleVersion{1, 0, 0}));
    EXPECT_EQ(runtime->Descriptor.Version, (ModuleVersion{1, 0, 0}));
    ASSERT_EQ(runtime->Bindings.size(), 1U);
    EXPECT_EQ(runtime->Bindings.front().Dependency, foundation->Descriptor.Id);
    EXPECT_EQ(runtime->Bindings.front().Reason, DependencyBindingReason::kRequired);
    for (const auto handle : handles)
    {
        EXPECT_EQ(manager->Inspect(handle)->State, ModuleState::kLoaded);
        EXPECT_TRUE(manager->Inspect(handle)->Resident);
        {
            const auto rejected = manager->BeginUnload(handle, report);
            EXPECT_FALSE(rejected);
            if (!rejected)
            {
                EXPECT_EQ(rejected.error(), MakeModuleError(ModuleError::kResidentModule));
            }
        }
        ASSERT_TRUE(manager->Register(handle, report));
        ASSERT_TRUE(manager->Activate(handle, report));
    }
    ASSERT_TRUE(manager->BeginShutdown(report));
    EXPECT_TRUE(manager->PollCleanup(report));
    for (const auto handle : handles)
    {
        EXPECT_TRUE(manager->Inspect(handle));
    }
    ASSERT_TRUE(manager->TryFinishShutdown(report));
    for (const auto handle : handles)
    {
        EXPECT_FALSE(manager->Inspect(handle));
    }
}

TEST(ResidentModule, PreconditionsDoNotRequestShutdown)
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    const auto runtimeFound = manager->Find(kModuleRuntimeModuleId);
    ASSERT_TRUE(runtimeFound);
    const auto runtime = *runtimeFound;
    {
        const auto rejected = manager->Register(runtime, report);
        EXPECT_FALSE(rejected);
        if (!rejected)
        {
            EXPECT_EQ(rejected.error(), MakeModuleError(ModuleError::kDependencyNotRegistered));
        }
    }
    EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kNone);
    EXPECT_EQ(manager->Inspect(runtime)->State, ModuleState::kLoaded);
    EXPECT_TRUE(manager->CreateScope(pg::service::Scope::kContext, manager->HostScope(), report));
    {
        const auto rejected = manager->TryFinishShutdown(report);
        EXPECT_FALSE(rejected);
        if (!rejected)
        {
            EXPECT_EQ(rejected.error(), MakeModuleError(ModuleError::kInvalidState));
        }
    }
}

TEST(ResidentModule, RegisterFailureRequestsShutdownAndRetainsOriginal)
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    const std::array queries{QueryFoundationModule, QueryModuleRuntime, QueryFailure};
    auto manager = ModuleManager::Create(declarations, queries, {}, report);
    ASSERT_TRUE(manager);
    const auto handleFound = manager->Find(kResident);
    ASSERT_TRUE(handleFound);
    const auto handle = *handleFound;
    {
        const auto rejected = manager->Register(handle, report);
        EXPECT_FALSE(rejected);
        if (!rejected)
        {
            EXPECT_EQ(rejected.error(), kFailure);
        }
    }
    EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kManagerShutdown);
    {
        const auto rejected = manager->TryFinishUnload(handle, report);
        EXPECT_FALSE(rejected);
        if (!rejected)
        {
            EXPECT_EQ(rejected.error(), MakeModuleError(ModuleError::kResidentModule));
        }
    }
    EXPECT_FALSE(manager->CreateScope(pg::service::Scope::kContext, manager->HostScope(), report));
    report = {};
    ASSERT_TRUE(manager->PollCleanup(report));
    EXPECT_EQ(report.OriginalError, kFailure);
    EXPECT_TRUE(manager->Inspect(handle));
    ASSERT_TRUE(manager->TryFinishShutdown(report));
    EXPECT_EQ(report.OriginalError, kFailure);
    EXPECT_FALSE(manager->Inspect(handle));
    EXPECT_FALSE(manager->Load({}, {}, report));
}

TEST(ResidentModule, ResidentBusyShutdownRetainsOriginalAfterReportDestruction)
{
    using namespace pg::test::scope_lifecycle;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    ResidentFixture::gState = state;
    const std::array queries{QueryFoundationModule, QueryModuleRuntime, ResidentFixture::Query};
    auto manager = ModuleManager::Create(declarations, queries, {}, report);
    ASSERT_TRUE(manager);
    const auto handleFound = manager->Find(kResident);
    ASSERT_TRUE(handleFound);
    const auto handle = *handleFound;
    const auto roots = manager->Modules();
    for (const auto root : roots)
    {
        if (root != handle)
        {
            ASSERT_TRUE(manager->Register(root, report));
            ASSERT_TRUE(manager->Activate(root, report));
        }
    }
    auto rootPin = manager->TryPinForBusiness(roots.front());
    ASSERT_TRUE(rootPin);
    ASSERT_TRUE(manager->Register(handle, report));
    ASSERT_TRUE(manager->CreateScope(pg::service::Scope::kContext, manager->HostScope(), report));
    ASSERT_TRUE(manager->CreateScope(pg::service::Scope::kContext, manager->HostScope(), report));
    state->FailStartAt = 2;
    state->Finished = false;
    {
        ModuleOperationReport failedReport;
        const auto rejected = manager->Activate(handle, failedReport);
        EXPECT_FALSE(rejected);
        if (!rejected)
        {
            EXPECT_EQ(rejected.error(), pg::test::scope_lifecycle::kFailure);
        }
        EXPECT_EQ(failedReport.CleanupOwner, ModuleCleanupOwner::kManagerShutdown);
    }
    EXPECT_FALSE(manager->TryPinForBusiness(roots.front()));
    EXPECT_FALSE(manager->TryFinishUnload(handle, report));
    EXPECT_EQ(report.PrimaryError, MakeModuleError(ModuleError::kResidentModule));
    EXPECT_FALSE(state->Exports.front().Access.TryBorrow());
    EXPECT_FALSE(manager->TryFinishShutdown(report));
    EXPECT_TRUE(manager->Inspect(handle));
    EXPECT_EQ(state->Destroyed, 0);
    report = {};
    EXPECT_FALSE(manager->PollCleanup(report));
    EXPECT_EQ(report.OriginalError, pg::test::scope_lifecycle::kFailure);
    state->Finished = true;
    EXPECT_FALSE(manager->PollCleanup(report));
    EXPECT_EQ(state->Stopped, 2);
    EXPECT_EQ(state->Destroyed, 2);
    EXPECT_TRUE(manager->Inspect(handle));
    for (const auto root : roots)
    {
        EXPECT_TRUE(manager->Inspect(root));
    }
    rootPin->Release();
    ASSERT_TRUE(manager->PollCleanup(report));
    EXPECT_EQ(report.OriginalError, pg::test::scope_lifecycle::kFailure);
    ASSERT_TRUE(manager->TryFinishShutdown(report));
    EXPECT_EQ(report.OriginalError, pg::test::scope_lifecycle::kFailure);
    EXPECT_FALSE(manager->Inspect(handle));
    ResidentFixture::gState.reset();
}
} // namespace pg::module
