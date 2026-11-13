#include "Fixtures/ModuleDiagnosticsFixture.h"
#include "ModuleBuiltin.h"
#include "ModuleDiagnostics.h"
#include "ModuleHandle.h"
#include "ModuleOperation.h"
#include "RegistrationContext.h"
#include "ScopeIdentity.h"
#include <ModuleDescriptor.h>
#include <ModuleIdentity.h>
#include <ScopeAccess.h>
#include <algorithm>
#include <cstddef>
#include <gtest/gtest.h>
#include <optional>
#include <span>
#include <utility>
#include <vector>
namespace
{
using namespace pg;
using namespace pg::module;
using namespace pg::test::module_diagnostics;
bool Has(const ModuleOperationReport& report, ModuleWaitKind kind)
{
    return std::ranges::any_of(std::span(report.WaitReasons).first(report.WaitReasonCount),
                               [&](const auto& reason) { return reason.Kind == kind; });
}

struct CrossModuleFixture
{
    inline static Controls* gFirst{};
    inline static Controls* gSecond{};
    static constexpr ModuleId kFirst{.Bytes = {std::byte{0xa1}}};
    static constexpr ModuleId kSecond{.Bytes = {std::byte{0xa2}}};
    static const ModuleDefinition* QueryFirst() noexcept
    {
        static const ModuleDefinition kDefinition{
            .Descriptor = {.Id = kFirst, .Dependencies = {}, .DisplayName = {}, .BuildIdentity = {}},
            .RegisterModule = [](RegistrationContext& context) noexcept { return RegisterControls(context, *gFirst); }};
        return &kDefinition;
    }
    static const ModuleDefinition* QuerySecond() noexcept
    {
        static const ModuleDefinition kDefinition{
            .Descriptor = {.Id = kSecond, .Dependencies = {}, .DisplayName = {}, .BuildIdentity = {}},
            .RegisterModule = [](RegistrationContext& context) noexcept
            { return RegisterControls(context, *gSecond); }};
        return &kDefinition;
    }
};

TEST(ModuleWait, OriginalFailureDoesNotHideAnotherModulesShutdownBlocker)
{
    for (const bool holdBorrow : {false, true})
    {
        Controls first;
        Controls second;
        CrossModuleFixture::gFirst = &first;
        CrossModuleFixture::gSecond = &second;
        first.FailStart = true;
        second.DeclarationPrefix = std::byte{0x9f};
        second.Done = holdBorrow;
        RegistrationScope declarations;
        ModuleOperationReport report;
        std::vector<QueryModuleFn> queries(BuiltinResidentQueries().begin(), BuiltinResidentQueries().end());
        queries.push_back(CrossModuleFixture::QueryFirst);
        queries.push_back(CrossModuleFixture::QuerySecond);
        auto manager = ModuleManager::Create(declarations, queries, {}, report);
        ASSERT_TRUE(manager);
        const auto firstHandle = manager->Find(CrossModuleFixture::kFirst).value_or(ModuleHandle{});
        const auto secondHandle = manager->Find(CrossModuleFixture::kSecond).value_or(ModuleHandle{});
        ASSERT_TRUE(manager->Register(secondHandle, report));
        ASSERT_TRUE(manager->Activate(secondHandle, report));
        std::optional<ScopeBorrow<Object>> borrow;
        if (holdBorrow)
        {
            auto acquired = second.Access.front().TryBorrow();
            ASSERT_TRUE(acquired);
            borrow.emplace(std::move(*acquired));
        }
        ASSERT_TRUE(manager->Register(firstHandle, report));
        EXPECT_FALSE(manager->Activate(firstHandle, report));
        EXPECT_FALSE(manager->TryFinishShutdown(report));
        EXPECT_EQ(report.Module, CrossModuleFixture::kFirst);
        EXPECT_EQ(report.OriginalError, kOriginal);
        const auto expected = holdBorrow ? ModuleWaitKind::kBorrow : ModuleWaitKind::kStop;
        EXPECT_TRUE(
            std::ranges::any_of(std::span(report.WaitReasons).first(report.WaitReasonCount), [&](const auto& reason)
                                { return reason.Module == CrossModuleFixture::kSecond && reason.Kind == expected; }));
        second.Done = true;
        borrow.reset();
        EXPECT_TRUE(manager->TryFinishShutdown(report));
    }
}
TEST(ModuleWait, BorrowPinDependencyAndNativeCloseAreDistinct)
{
    Controls controls;
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = Create(declarations, report);
    ASSERT_TRUE(manager);
    const auto handle = manager->Find(kOwner).value_or(ModuleHandle{});
    ASSERT_TRUE(manager->Register(handle, report));
    ASSERT_TRUE(manager->Activate(handle, report));
    auto borrow = controls.Access.front().TryBorrow();
    auto pin = manager->TryPinForBusiness(handle);
    ASSERT_TRUE(borrow);
    ASSERT_TRUE(pin);
    auto context = manager->CreateScope(service::Scope::kContext, manager->HostScope(), report);
    ASSERT_TRUE(context);
    auto const world = manager->CreateScope(service::Scope::kWorld, *context, report);
    ASSERT_TRUE(world);
    ASSERT_TRUE(manager->BeginShutdown(report));
    EXPECT_FALSE(manager->TryFinishShutdown(report));
    EXPECT_TRUE(Has(report, ModuleWaitKind::kBorrow));
    EXPECT_TRUE(Has(report, ModuleWaitKind::kModulePin));
    EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kManagerShutdown);
    borrow->Release();
    pin->Release();
    ASSERT_TRUE(manager->TryFinishShutdown(report));
}
TEST(ModuleWait, SnapshotTruncationPreservesOwner)
{
    Controls controls;
    controls.ResourceCount = 12;
    controls.Done = false;
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = Create(declarations, report);
    ASSERT_TRUE(manager);
    const auto handle = manager->Find(kOwner).value_or(ModuleHandle{});
    ASSERT_TRUE(manager->Register(handle, report));
    ASSERT_TRUE(manager->Activate(handle, report));
    ASSERT_TRUE(manager->BeginShutdown(report));
    EXPECT_FALSE(manager->TryFinishShutdown(report));
    EXPECT_EQ(report.WaitReasonCount, 8U);
    EXPECT_EQ(report.RemainingWaitReasons, 4U);
    EXPECT_TRUE(report.WaitReasonsTruncated);
    EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kManagerShutdown);
    EXPECT_EQ(report.WaitReasons.front().Module, kOwner);
    const auto first = report.WaitReasons.front().Declaration;
    EXPECT_FALSE(manager->TryFinishShutdown(report));
    EXPECT_EQ(report.WaitReasons.front().Declaration, first);
    controls.Done = true;
    ASSERT_TRUE(manager->TryFinishShutdown(report));
}

TEST(ModuleWait, DependencyWithNoResourcesKeepsProviderIdentity)
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    for (const auto handle : manager->RegistrationOrder())
    {
        ASSERT_TRUE(manager->Register(handle, report));
        ASSERT_TRUE(manager->Activate(handle, report));
    }
    const auto runtime = manager->Find(kModuleRuntimeModuleId).value_or(ModuleHandle{});
    auto pin = manager->TryPinForBusiness(runtime);
    ASSERT_TRUE(pin);
    ASSERT_TRUE(manager->BeginShutdown(report));
    EXPECT_FALSE(manager->TryFinishShutdown(report));
    EXPECT_TRUE(Has(report, ModuleWaitKind::kDependency));
    EXPECT_TRUE(Has(report, ModuleWaitKind::kModulePin));
    pin->Release();
    EXPECT_TRUE(manager->TryFinishShutdown(report));
}

TEST(ModuleWait, PrimaryPollOriginSurvivesFullSecondaryCache)
{
    Controls controls;
    controls.ResourceCount = 12;
    controls.Done = false;
    controls.PollError = true;
    controls.FailPollAt = 0;
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = Create(declarations, report);
    ASSERT_TRUE(manager);
    const auto handle = manager->Find(kOwner).value_or(ModuleHandle{});
    ASSERT_TRUE(manager->Register(handle, report));
    ASSERT_TRUE(manager->Activate(handle, report));
    ASSERT_TRUE(manager->BeginShutdown(report));
    const auto pending = manager->TryFinishShutdown(report);
    ASSERT_FALSE(pending);
    EXPECT_EQ(pending.error(), kCleanup);
    EXPECT_EQ(report.PrimaryError, kCleanup);
    EXPECT_EQ(report.FailureOrigin, ModuleFailureOrigin::kPollStopped);
    ASSERT_TRUE(report.Failure.PrimarySite());
    EXPECT_EQ(report.Failure.PrimarySite()->Cause, kCleanup);
    EXPECT_TRUE(report.Failure.PrimarySite()->OriginKnown());
    controls.Done = true;
    EXPECT_TRUE(manager->TryFinishShutdown(report));
}

TEST(ModuleWait, ParentScopeExplainsItsOutstandingChild)
{
    Controls controls;
    controls.Kind = service::Scope::kWorld;
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = Create(declarations, report);
    ASSERT_TRUE(manager);
    const auto handle = manager->Find(kOwner).value_or(ModuleHandle{});
    ASSERT_TRUE(manager->Register(handle, report));
    ASSERT_TRUE(manager->Activate(handle, report));
    const auto context = manager->CreateScope(service::Scope::kContext, manager->HostScope(), report);
    ASSERT_TRUE(context);
    const auto world = manager->CreateScope(service::Scope::kWorld, *context, report);
    ASSERT_TRUE(world);
    auto borrow = controls.Access.back().TryBorrow();
    ASSERT_TRUE(borrow);
    ASSERT_TRUE(manager->BeginCloseScope(*context, report));
    EXPECT_FALSE(manager->TryFinishCloseScope(*context, report));
    EXPECT_TRUE(Has(report, ModuleWaitKind::kChildScope));
    EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kCallerUnload);
    EXPECT_EQ(report.Scope, *context);
    borrow->Release();
    EXPECT_TRUE(manager->TryFinishCloseScope(*context, report));
}
} // namespace
