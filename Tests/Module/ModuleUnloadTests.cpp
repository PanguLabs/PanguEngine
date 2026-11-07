#include "Error.h"
#include "ModuleDeclaration.h"
#include "ModuleDescriptor.h"
#include "ModuleIdentity.h"
#include "ModuleOperation.h"
#include "ModuleTestAccess.h"
#include "NativeModuleLibrary.h"
#include "Result.h"
#include "ScopeBuildContext.h"
#include "ScopeFactory.h"
#include "ScopeIdentity.h"
#include "SharedFixtureContract.h"
#include "SharedFixturePaths.h"
#include <ModuleBuiltin.h>
#include <ModuleError.h>
#include <ModuleManager.h>
#include <RegistrationContext.h>
#include <ScopeTestSupport.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace pg::module
{
using namespace fixture;
namespace
{
constexpr ModuleId kResidentConsumer{.Bytes = {std::byte{0x74}}};
const ModuleDefinition* QueryResidentConsumer() noexcept
{
    static constexpr ModuleDefinition kDefinition{
        .Descriptor = {.Id = kResidentConsumer, .Dependencies = {}, .DisplayName = {}, .BuildIdentity = {}},
        .RegisterModule = [](RegistrationContext& context) noexcept -> Status
        {
            constexpr DeclarationId kObject{.Bytes = {std::byte{0x74}}};
            const std::array dependencies{ScopeDependency{.Id = kSharedProviderDeclaration}};
            return context.DeclareFactory(
                kObject, pg::service::Scope::kHost,
                ScopeFactory(
                    [](ScopeBuildContext& build) noexcept -> Status
                    {
                        const auto dependency = build.Dependency<pg::test::ProbeState>(kSharedProviderDeclaration);
                        if (!dependency)
                        {
                            return std::unexpected(dependency.error());
                        }
                        const auto object = build.Own(std::make_unique<int>((*dependency)->Values.front()));
                        return object ? Status{} : Status{std::unexpected(object.error())};
                    }),
                dependencies);
        },
    };
    return &kDefinition;
}
} // namespace

TEST(ModuleUnload, DrainedResidentWithPinKeepsBusinessProviderDependency)
{
    auto& observer = pg::test::Observer();
    observer.Reset();
    observer.ConfigureSharedFixture({.Services = true});
    RegistrationScope declarations;
    ModuleOperationReport report;
    std::vector<QueryModuleFn> queries(BuiltinResidentQueries().begin(), BuiltinResidentQueries().end());
    queries.push_back(QueryResidentConsumer);
    auto manager = ModuleManager::Create(declarations, queries, {}, report);
    ASSERT_TRUE(manager);
    const std::array candidates{
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedProviderId, .Path = kSharedProviderPath}},
    };
    auto loaded = manager->Load(candidates, {}, report);
    ASSERT_TRUE(loaded);
    const auto provider = loaded->Modules.front();
    const auto consumer = manager->Find(kResidentConsumer).value_or(ModuleHandle{});
    ASSERT_TRUE(manager->Register(provider, report));
    ASSERT_TRUE(manager->Activate(provider, report));
    ASSERT_TRUE(manager->Register(consumer, report));
    ASSERT_TRUE(manager->Activate(consumer, report));
    auto pin = manager->TryPinForBusiness(consumer);
    ASSERT_TRUE(pin);
    ASSERT_TRUE(manager->BeginShutdown(report));
    EXPECT_FALSE(manager->PollCleanup(report));
    EXPECT_FALSE(manager->PollCleanup(report));
    EXPECT_TRUE(manager->Inspect(provider));
    EXPECT_EQ(observer.Count(pg::test::Event::kDestroy), 0U);
    EXPECT_EQ(observer.NativeUnloadCount(), 0U);
    pin->Release();
    ASSERT_TRUE(manager->PollCleanup(report));
    EXPECT_FALSE(manager->Inspect(provider));
    EXPECT_TRUE(manager->Inspect(consumer));
    EXPECT_EQ(observer.NativeUnloadCount(), 1U);
    ASSERT_TRUE(manager->TryFinishShutdown(report));
}

TEST(ModuleUnload, FailedBatchRetainsUnreleasedReservation)
{
    auto& observer = pg::test::Observer();
    observer.Reset();
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    for (const auto handle : manager->Modules())
    {
        ASSERT_TRUE(manager->Register(handle, report));
        ASSERT_TRUE(manager->Activate(handle, report));
    }
    const auto oldActive = manager->Modules().back();
    const std::array candidates{
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedProviderId, .Path = kSharedProviderPath}},
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedConsumerId, .Path = kMissingQueryPath}},
    };
    detail::ModuleManagerTestAccess::FailNextRollbackClose(*manager);
    std::optional<Error> original;
    {
        ModuleOperationReport failedReport;
        const auto loadResult = manager->Load(candidates, {}, failedReport);
        EXPECT_FALSE(loadResult.has_value());
        ASSERT_FALSE(loadResult);
        original = loadResult.error();
        EXPECT_EQ(failedReport.CleanupOwner, ModuleCleanupOwner::kManagerCleanup);
        EXPECT_EQ(failedReport.OriginalError, original);
        EXPECT_EQ(original->Code(), static_cast<unsigned>(ModuleError::kQuerySymbolMissing));
        EXPECT_EQ(failedReport.CleanupError, MakeModuleError(ModuleError::kLibraryCloseFailed));
        EXPECT_EQ(failedReport.Handle, ModuleHandle{});
    }
    EXPECT_EQ(observer.NativeUnloadCount(), 0U);
    EXPECT_EQ(observer.RegisterCount(), 0U);
    EXPECT_EQ(manager->Modules().size(), 2U);
    EXPECT_FALSE(manager->Find(kSharedProviderId));
    EXPECT_FALSE(manager->Find(kSharedConsumerId));
    for (const auto& candidate : candidates)
    {
        const auto duplicate = manager->Load(std::span(&candidate, 1), {}, report);
        EXPECT_FALSE(duplicate);
        if (!duplicate)
        {
            EXPECT_EQ(duplicate.error(), MakeModuleError(ModuleError::kDuplicateModule));
        }
    }
    const std::vector<ModuleLoadCandidate> overCapacity(253, candidates.front());
    const auto capacity = manager->Load(overCapacity, {}, report);
    EXPECT_FALSE(capacity);
    if (!capacity)
    {
        EXPECT_EQ(capacity.error(), MakeModuleError(ModuleError::kLimitExceeded));
    }
    EXPECT_TRUE(manager->TryPinForBusiness(oldActive));
    EXPECT_TRUE(manager->CreateScope(pg::service::Scope::kContext, manager->HostScope(), report));
    EXPECT_EQ(manager->Inspect(oldActive)->State, ModuleState::kActive);
    ASSERT_TRUE(manager->PollCleanup(report));
    EXPECT_EQ(report.OriginalError, original);
    EXPECT_EQ(observer.NativeUnloadCount(), 1U);
    auto reloaded = manager->Load(std::span(candidates).first(1), {}, report);
    ASSERT_TRUE(reloaded);
    EXPECT_GT(reloaded->Modules.front().Generation, 1U);
    ASSERT_TRUE(manager->BeginUnload(reloaded->Modules.front(), report));
    ASSERT_TRUE(manager->TryFinishUnload(reloaded->Modules.front(), report));
    ASSERT_TRUE(manager->PollCleanup(report));
    EXPECT_FALSE(report.OriginalError);
}

TEST(ModuleUnload, FailedBatchPreservesActiveProviderUntilRealRetry)
{
    auto& observer = pg::test::Observer();
    observer.Reset();
    observer.ConfigureSharedFixture({.Services = true});
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    const std::array providerCandidate{
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedProviderId, .Path = kSharedProviderPath}},
    };
    auto loaded = manager->Load(providerCandidate, {}, report);
    ASSERT_TRUE(loaded);
    const auto provider = loaded->Modules.front();
    ASSERT_TRUE(manager->Register(provider, report));
    ASSERT_TRUE(manager->Activate(provider, report));
    constexpr ModuleId kMissing{.Bytes = {std::byte{0x73}}};
    const std::array failedCandidates{
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedConsumerId, .Path = kSharedConsumerPath}},
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kMissing, .Path = kMissingQueryPath}},
    };
    detail::ModuleManagerTestAccess::FailNextRollbackClose(*manager);
    EXPECT_FALSE(manager->Load(failedCandidates, {}, report));
    EXPECT_EQ(report.Handle, ModuleHandle{});
    EXPECT_EQ(observer.NativeUnloadCount(), 0U);
    const auto rejected = manager->BeginUnload(provider, report);
    EXPECT_FALSE(rejected);
    EXPECT_EQ(manager->Inspect(provider)->State, ModuleState::kActive);
    EXPECT_TRUE(manager->TryPinForBusiness(provider));
    {
        auto borrow = observer.Access().TryBorrow();
        ASSERT_TRUE(borrow);
        EXPECT_EQ((*borrow)->Values.front(), 42);
    }
    EXPECT_EQ(observer.Count(pg::test::Event::kStop), 0U);
    EXPECT_EQ(observer.Count(pg::test::Event::kDestroy), 0U);
    ASSERT_TRUE(manager->PollCleanup(report));
    EXPECT_EQ(observer.NativeUnloadCount(), 1U);
    EXPECT_TRUE(manager->TryPinForBusiness(provider));
    ASSERT_TRUE(manager->BeginUnload(provider, report));
    ASSERT_TRUE(manager->TryFinishUnload(provider, report));
    EXPECT_EQ(observer.NativeUnloadCount(), 2U);
    EXPECT_EQ(observer.Count(pg::test::Event::kDestroy), 1U);
}

TEST(ModuleUnload, FailedBatchProtectsClosingScopeFromCreateScopeFailureCleanup)
{
    for (const bool failFactory : {true, false})
    {
        SCOPED_TRACE(failFactory ? "Factory failure" : "Start failure");
        auto& observer = pg::test::Observer();
        observer.Reset();
        observer.ConfigureSharedFixture({.Services = true, .ProviderInContext = true});
        RegistrationScope declarations;
        ModuleOperationReport report;
        auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
        ASSERT_TRUE(manager);
        const std::array candidates{
            ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedProviderId, .Path = kSharedProviderPath}},
        };
        auto loaded = manager->Load(candidates, {}, report);
        ASSERT_TRUE(loaded);
        const auto provider = loaded->Modules.front();
        ASSERT_TRUE(manager->Register(provider, report));
        ASSERT_TRUE(manager->Activate(provider, report));
        const auto scopeA = manager->CreateScope(pg::service::Scope::kContext, manager->HostScope(), report);
        ASSERT_TRUE(scopeA);
        constexpr ModuleId kMissing{.Bytes = {std::byte{0x73}}};
        const std::array failedCandidates{
            ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedConsumerId, .Path = kSharedConsumerPath}},
            ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kMissing, .Path = kMissingQueryPath}},
        };
        detail::ModuleManagerTestAccess::FailNextRollbackClose(*manager);
        const auto failedBatch = manager->Load(failedCandidates, {}, report);
        ASSERT_FALSE(failedBatch);
        EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kManagerCleanup);
        ASSERT_TRUE(manager->BeginCloseScope(*scopeA, report));
        EXPECT_FALSE(manager->TryFinishCloseScope(*scopeA, report));
        EXPECT_EQ(report.PrimaryError, MakeModuleError(ModuleError::kBusy));
        const auto scopeC = manager->CreateScope(pg::service::Scope::kContext, manager->HostScope(), report);
        ASSERT_TRUE(scopeC);
        const auto accessC = observer.Access();
        EXPECT_TRUE(manager->TryPinForBusiness(provider));
        observer.ConfigureSharedFixture({
            .Services = true,
            .ProviderInContext = true,
            .FailProviderFactory = failFactory,
            .FailProviderStart = !failFactory,
        });
        {
            ModuleOperationReport failure;
            const auto scopeB = manager->CreateScope(pg::service::Scope::kContext, manager->HostScope(), failure);
            EXPECT_FALSE(scopeB);
            EXPECT_EQ(failure.OriginalError, MakeModuleError(ModuleError::kInvalidState));
            EXPECT_EQ(failure.Progress, ModuleOperationProgress::kCleanupPending);
            EXPECT_EQ(failure.CleanupOwner, ModuleCleanupOwner::kManagerCleanup);
        }
        EXPECT_EQ(observer.NativeUnloadCount(), 0U);
        EXPECT_EQ(observer.Count(pg::test::Event::kStop), 0U);
        EXPECT_EQ(observer.Count(pg::test::Event::kDestroy), 0U);
        // 重复关闭仍能解析 A，证明内部回滚没有提前回收其 Scope 槽位。
        EXPECT_TRUE(manager->BeginCloseScope(*scopeA, report));
        EXPECT_FALSE(manager->TryFinishCloseScope(*scopeA, report));
        EXPECT_TRUE(manager->TryPinForBusiness(provider));
        {
            auto borrow = accessC.TryBorrow();
            ASSERT_TRUE(borrow);
            EXPECT_EQ((*borrow)->Values.front(), 42);
        }
        ASSERT_TRUE(manager->PollCleanup(report));
        EXPECT_EQ(report.OriginalError, failedBatch.error());
        EXPECT_EQ(observer.NativeUnloadCount(), 1U);
        EXPECT_EQ(observer.Count(pg::test::Event::kStop), failFactory ? 1U : 2U);
        EXPECT_EQ(observer.Count(pg::test::Event::kDestroy), failFactory ? 1U : 2U);
        EXPECT_FALSE(manager->BeginCloseScope(*scopeA, report));
        EXPECT_EQ(report.PrimaryError, MakeModuleError(ModuleError::kScopeNotFound));
        EXPECT_TRUE(accessC.TryBorrow());
        ASSERT_TRUE(manager->BeginCloseScope(*scopeC, report));
        ASSERT_TRUE(manager->TryFinishCloseScope(*scopeC, report));
        ASSERT_TRUE(manager->BeginUnload(provider, report));
        ASSERT_TRUE(manager->TryFinishUnload(provider, report));
        EXPECT_EQ(observer.NativeUnloadCount(), 2U);
        ASSERT_TRUE(manager->PollCleanup(report));
        EXPECT_FALSE(report.OriginalError);
    }
}

TEST(ModuleUnload, FailedBatchProtectsEmptyScopeFromActivateFailureCleanup)
{
    auto& observer = pg::test::Observer();
    observer.Reset();
    observer.ConfigureSharedFixture({.Services = true, .ProviderInContext = true});
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    const auto scopeA = manager->CreateScope(pg::service::Scope::kContext, manager->HostScope(), report);
    const auto scopeB = manager->CreateScope(pg::service::Scope::kContext, manager->HostScope(), report);
    ASSERT_TRUE(scopeA);
    ASSERT_TRUE(scopeB);
    const std::array candidates{
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedProviderId, .Path = kSharedProviderPath}},
    };
    auto loaded = manager->Load(candidates, {}, report);
    ASSERT_TRUE(loaded);
    const auto provider = loaded->Modules.front();
    ASSERT_TRUE(manager->Register(provider, report));
    constexpr ModuleId kMissing{.Bytes = {std::byte{0x73}}};
    const std::array failedCandidates{
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedConsumerId, .Path = kSharedConsumerPath}},
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kMissing, .Path = kMissingQueryPath}},
    };
    detail::ModuleManagerTestAccess::FailNextRollbackClose(*manager);
    ASSERT_FALSE(manager->Load(failedCandidates, {}, report));
    ASSERT_TRUE(manager->BeginCloseScope(*scopeA, report));
    observer.ConfigureSharedFixture({.Services = true, .ProviderInContext = true, .FailProviderStart = true});
    {
        ModuleOperationReport failure;
        EXPECT_FALSE(manager->Activate(provider, failure));
        EXPECT_EQ(failure.OriginalError, MakeModuleError(ModuleError::kInvalidState));
        EXPECT_EQ(failure.Progress, ModuleOperationProgress::kCleanupPending);
    }
    EXPECT_EQ(observer.NativeUnloadCount(), 0U);
    EXPECT_EQ(observer.Count(pg::test::Event::kStop), 0U);
    EXPECT_EQ(observer.Count(pg::test::Event::kDestroy), 0U);
    EXPECT_TRUE(manager->BeginCloseScope(*scopeA, report));
    ASSERT_TRUE(manager->PollCleanup(report));
    EXPECT_EQ(report.OriginalError, MakeModuleError(ModuleError::kInvalidState));
    EXPECT_EQ(observer.NativeUnloadCount(), 2U);
    EXPECT_EQ(observer.Count(pg::test::Event::kStop), 1U);
    EXPECT_EQ(observer.Count(pg::test::Event::kDestroy), 1U);
    EXPECT_FALSE(manager->Inspect(provider));
    EXPECT_FALSE(manager->BeginCloseScope(*scopeA, report));
    EXPECT_EQ(report.PrimaryError, MakeModuleError(ModuleError::kScopeNotFound));
    ASSERT_TRUE(manager->BeginCloseScope(*scopeB, report));
    ASSERT_TRUE(manager->TryFinishCloseScope(*scopeB, report));
}

TEST(ModuleUnload, FailedBatchKeepsWholeReservationForBothNativeDependencyInputOrders)
{
    for (const bool consumerFirst : {false, true})
    {
        auto& observer = pg::test::Observer();
        observer.Reset();
        RegistrationScope declarations;
        ModuleOperationReport report;
        auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
        ASSERT_TRUE(manager);
        constexpr ModuleId kMissing{.Bytes = {std::byte{0x73}}};
        std::array candidates{
            ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedProviderId, .Path = kSharedProviderPath}},
            ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedConsumerId, .Path = kSharedConsumerPath}},
            ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kMissing, .Path = kMissingQueryPath}},
        };
        if (consumerFirst)
        {
            std::swap(candidates.front(), candidates.at(1));
        }
        detail::ModuleManagerTestAccess::FailNextRollbackClose(*manager);
        const auto failed = manager->Load(candidates, {}, report);
        EXPECT_FALSE(failed);
        EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kManagerCleanup);
        EXPECT_EQ(observer.NativeUnloadCount(), consumerFirst ? 0U : 1U);
        EXPECT_EQ(observer.RegisterCount(), 0U);
        EXPECT_EQ(manager->Modules().size(), 2U);
        for (const auto& candidate : candidates)
        {
            const auto duplicate = manager->Load(std::span(&candidate, 1), {}, report);
            EXPECT_FALSE(duplicate);
            EXPECT_EQ(report.PrimaryError, MakeModuleError(ModuleError::kDuplicateModule));
        }
        // 未交付记录即使猜出槽位/代际也不能成为操作入口。
        const auto root = manager->Modules().front();
        for (std::uint32_t slot = 3; slot != 6; ++slot)
        {
            const ModuleHandle guessed{.Domain = root.Domain, .Slot = slot, .Generation = 1};
            EXPECT_FALSE(manager->Inspect(guessed));
            EXPECT_FALSE(manager->Register(guessed, report));
            EXPECT_FALSE(manager->TryPinForBusiness(guessed));
        }
        ASSERT_TRUE(manager->PollCleanup(report));
        EXPECT_EQ(observer.NativeUnloadCount(), 2U);
        auto reloaded = manager->Load(std::span(candidates).first(2), {}, report);
        ASSERT_TRUE(reloaded);
        for (const auto handle : reloaded->Modules)
        {
            EXPECT_GT(handle.Generation, 1U);
        }
        ASSERT_TRUE(manager->BeginShutdown(report));
        ASSERT_TRUE(manager->TryFinishShutdown(report));
        EXPECT_EQ(observer.NativeUnloadCount(), 4U);
    }
}

TEST(ModuleUnload, RejectedMetadataCycleReleasesRealNativeDependencyAndCanReload)
{
    for (const bool failClose : {false, true})
    {
        auto& observer = pg::test::Observer();
        observer.Reset();
        observer.ConfigureSharedFixture({.MetadataCycle = true});
        RegistrationScope declarations;
        ModuleOperationReport report;
        auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
        ASSERT_TRUE(manager);
        const std::array candidates{
            ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedConsumerId, .Path = kSharedConsumerPath}},
            ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedProviderId, .Path = kSharedProviderPath}},
        };
        if (failClose)
        {
            detail::ModuleManagerTestAccess::FailNextRollbackClose(*manager);
        }
        const auto failed = manager->Load(candidates, {}, report);
        EXPECT_FALSE(failed);
        if (!failed)
        {
            EXPECT_EQ(failed.error(), MakeModuleError(ModuleError::kDependencyCycle));
        }
        EXPECT_EQ(observer.RegisterCount(), 0U);
        EXPECT_EQ(report.Handle, ModuleHandle{});
        if (failClose)
        {
            EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kManagerCleanup);
            EXPECT_EQ(observer.NativeUnloadCount(), 0U);
            EXPECT_FALSE(manager->Load(candidates, {}, report));
            EXPECT_EQ(report.PrimaryError, MakeModuleError(ModuleError::kDuplicateModule));
            ASSERT_TRUE(manager->PollCleanup(report));
            EXPECT_EQ(report.OriginalError, MakeModuleError(ModuleError::kDependencyCycle));
        }
        EXPECT_EQ(observer.NativeUnloadCount(), 2U);
        observer.ConfigureSharedFixture({});
        ASSERT_TRUE(manager->Load(candidates, {}, report));
        ASSERT_TRUE(manager->BeginShutdown(report));
        ASSERT_TRUE(manager->TryFinishShutdown(report));
        EXPECT_EQ(observer.NativeUnloadCount(), 4U);
    }
}

TEST(ModuleUnload, ExternalPinDoesNotBlockDestroyButNativeCloseFailureKeepsProvider)
{
    auto& observer = pg::test::Observer();
    observer.Reset();
    observer.ConfigureSharedFixture({.Services = true, .ConsumerBusyPolls = 2});
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    const auto roots = manager->Modules();
    ASSERT_TRUE(manager->CreateScope(pg::service::Scope::kContext, manager->HostScope(), report));
    const std::array candidates{
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedProviderId, .Path = kSharedProviderPath}},
        // 此场景要求观测物理卸载顺序，先显式持有 Provider，避免隐式装载组延迟移除。
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedConsumerId, .Path = kSharedConsumerPath}},
    };
    auto loaded = manager->Load(candidates, {}, report);
    ASSERT_TRUE(loaded);
    const auto provider = loaded->Modules.front();
    const auto consumer = loaded->Modules.back();
    for (const auto handle : loaded->Modules)
    {
        ASSERT_TRUE(manager->Register(handle, report));
        ASSERT_TRUE(manager->Activate(handle, report));
    }
    auto pin = manager->TryPinForBusiness(consumer);
    ASSERT_TRUE(pin);
    detail::ModuleManagerTestAccess::FailNextNativeClose(*manager, consumer);
    ASSERT_TRUE(manager->BeginShutdown(report));
    EXPECT_FALSE(manager->TryFinishShutdown(report));
    EXPECT_EQ(observer.Count(pg::test::Event::kConsumerDestroy), 0U);
    EXPECT_FALSE(manager->TryFinishShutdown(report));
    EXPECT_FALSE(manager->TryFinishShutdown(report));
    EXPECT_EQ(observer.Count(pg::test::Event::kConsumerDestroy), 1U);
    EXPECT_EQ(observer.Count(pg::test::Event::kConsumerStop), 1U);
    EXPECT_EQ(observer.Count(pg::test::Event::kDependencyUsed), 1U);
    EXPECT_EQ(observer.Count(pg::test::Event::kDestroy), 0U);
    EXPECT_EQ(observer.Count(pg::test::Event::kStop), 0U);
    EXPECT_EQ(observer.NativeUnloadCount(), 0U);
    std::thread release{[owned = std::move(*pin)] mutable { owned.Release(); }};
    release.join();
    EXPECT_EQ(observer.NativeUnloadCount(), 0U);
    const auto closeFailed = manager->TryFinishShutdown(report);
    EXPECT_FALSE(closeFailed);
    EXPECT_EQ(report.CleanupError, MakeModuleError(ModuleError::kLibraryCloseFailed));
    EXPECT_TRUE(manager->Inspect(consumer));
    EXPECT_TRUE(manager->Inspect(provider));
    EXPECT_EQ(observer.Count(pg::test::Event::kDestroy), 0U);
    EXPECT_EQ(observer.NativeUnloadCount(), 0U);
    ASSERT_TRUE(manager->PollCleanup(report));
    EXPECT_EQ(observer.NativeUnloadCount(), 2U);
    EXPECT_EQ(observer.Count(pg::test::Event::kDestroy), 1U);
    const auto events = observer.Events();
    EXPECT_LT(std::ranges::find(events, pg::test::Event::kConsumerDestroy),
              std::ranges::find(events, pg::test::Event::kNativeUnload));
    EXPECT_LT(std::ranges::find(events, pg::test::Event::kNativeUnload),
              std::ranges::find(events, pg::test::Event::kDestroy));
    for (const auto handle : roots)
    {
        EXPECT_TRUE(manager->Inspect(handle));
    }
    ASSERT_TRUE(manager->TryFinishShutdown(report));
    for (const auto handle : roots)
    {
        EXPECT_FALSE(manager->Inspect(handle));
    }
}

TEST(ModuleUnload, PartialStartFailureRetainsOriginalAcrossReportsAndExternalBorrow)
{
    auto& observer = pg::test::Observer();
    observer.Reset();
    observer.ConfigureSharedFixture({.Services = true, .FailSecondConsumerStart = true, .ConsumerBusyPolls = 3});
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    for (int index = 0; index != 2; ++index)
    {
        ASSERT_TRUE(manager->CreateScope(pg::service::Scope::kContext, manager->HostScope(), report));
    }
    const std::array candidates{
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedProviderId, .Path = kSharedProviderPath}},
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedConsumerId, .Path = kSharedConsumerPath}},
    };
    auto loaded = manager->Load(candidates, {}, report);
    ASSERT_TRUE(loaded);
    const auto provider = loaded->Modules.front();
    const auto consumer = loaded->Modules.back();
    ASSERT_TRUE(manager->Register(provider, report));
    ASSERT_TRUE(manager->Activate(provider, report));
    auto borrow = observer.Access().TryBorrow();
    ASSERT_TRUE(borrow);
    ASSERT_TRUE(manager->Register(consumer, report));
    {
        ModuleOperationReport failure;
        EXPECT_FALSE(manager->Activate(consumer, failure));
        EXPECT_EQ(failure.OriginalError, MakeModuleError(ModuleError::kInvalidState));
    }
    ASSERT_TRUE(manager->BeginUnload(consumer, report));
    EXPECT_FALSE(manager->TryFinishUnload(consumer, report));
    EXPECT_EQ(report.OriginalError, MakeModuleError(ModuleError::kInvalidState));
    EXPECT_FALSE(manager->PollCleanup(report));
    EXPECT_EQ(report.OriginalError, MakeModuleError(ModuleError::kInvalidState));
    ASSERT_TRUE(manager->PollCleanup(report));
    EXPECT_EQ(report.OriginalError, MakeModuleError(ModuleError::kInvalidState));
    EXPECT_EQ(observer.Count(pg::test::Event::kConsumerStop), 2U);
    EXPECT_EQ(observer.Count(pg::test::Event::kConsumerDestroy), 2U);
    EXPECT_EQ(observer.Count(pg::test::Event::kDependencyUsed), 2U);
    EXPECT_EQ(observer.NativeUnloadCount(), 1U);
    EXPECT_EQ((*borrow)->Values.front(), 42);
    ASSERT_TRUE(manager->BeginUnload(provider, report));
    EXPECT_FALSE(manager->TryFinishUnload(provider, report));
    EXPECT_EQ(observer.Count(pg::test::Event::kDestroy), 0U);
    borrow->Release();
    EXPECT_EQ(observer.NativeUnloadCount(), 1U);
    ASSERT_TRUE(manager->TryFinishUnload(provider, report));
    EXPECT_EQ(observer.NativeUnloadCount(), 2U);
}

TEST(ModuleUnload, ExtraNativeHoldDoesNotPreventSuccessfulManagerRelease)
{
    auto& observer = pg::test::Observer();
    observer.Reset();
    auto external = detail::NativeModuleLibrary::Open(kSharedProviderPath);
    ASSERT_TRUE(external);
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    const std::array candidates{
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedProviderId, .Path = kSharedProviderPath}},
    };
    auto loaded = manager->Load(candidates, {}, report);
    ASSERT_TRUE(loaded);
    const auto handle = loaded->Modules.front();
    ASSERT_TRUE(manager->BeginUnload(handle, report));
    ASSERT_TRUE(manager->TryFinishUnload(handle, report));
    EXPECT_FALSE(manager->Inspect(handle));
    EXPECT_EQ(observer.NativeUnloadCount(), 0U);
    ASSERT_TRUE(external->Release());
    EXPECT_EQ(observer.NativeUnloadCount(), 1U);
}
} // namespace pg::module
