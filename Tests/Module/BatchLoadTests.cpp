#include "ModuleHandle.h"
#include "ModuleOperation.h"
#include <ModuleBuiltin.h>
#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <ModuleManager.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeIdentity.h>
#include <array>
#include <cstddef>
#include <gtest/gtest.h>
#include <optional>
#include <vector>
#if defined(PG_TEST_MODULAR)
#include "SharedFixtureContract.h"
#include "SharedFixturePaths.h"
#include <ScopeTestSupport.h>
#endif

namespace pg::module
{
TEST(BatchLoad, EmptyBatchPreservesResidentSet)
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    const auto result = manager->Load({}, {}, report);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), MakeModuleError(ModuleError::kInvalidDescriptor));
    EXPECT_EQ(manager->Modules().size(), 2U);
    EXPECT_EQ(report.Handle, ModuleHandle{});
}
#if defined(PG_TEST_MODULAR)
TEST(BatchLoad, RealLibrariesCommitInDependencyOrderAndSnapshotsOutliveUnload)
{
    using namespace fixture;
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    const std::array candidates{
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedConsumerId, .Path = kSharedConsumerPath}},
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedProviderId, .Path = kSharedProviderPath}},
    };
    auto result = manager->Load(candidates, {}, report);
    ASSERT_TRUE(result);
    ASSERT_EQ(result->Modules.size(), 2U);
    const auto provider = result->Modules.front();
    const auto consumer = result->Modules.back();
    EXPECT_EQ(manager->Inspect(provider)->Descriptor.Id, kSharedProviderId);
    EXPECT_EQ(manager->Inspect(consumer)->Descriptor.Id, kSharedConsumerId);
    auto snapshot = manager->Inspect(consumer);
    EXPECT_EQ(manager->Inspect(consumer)->State, ModuleState::kLoaded);
    {
        const auto rejected = manager->BeginUnload(provider, report);
        EXPECT_FALSE(rejected);
        if (!rejected)
        {
            EXPECT_EQ(rejected.error(), MakeModuleError(ModuleError::kDependencyInUse));
        }
    }
    ASSERT_TRUE(manager->BeginUnload(consumer, report));
    ASSERT_TRUE(manager->TryFinishUnload(consumer, report));
    ASSERT_TRUE(manager->BeginUnload(provider, report));
    ASSERT_TRUE(manager->TryFinishUnload(provider, report));
    EXPECT_EQ(snapshot->Descriptor.Id, kSharedConsumerId);
    EXPECT_FALSE(snapshot->Descriptor.DisplayName.empty());
    EXPECT_EQ(snapshot->Bindings.front().Dependency, kSharedProviderId);
}

TEST(BatchLoad, NativeFailurePublishesNoPartialHandlesAndCanRetry)
{
    using namespace fixture;
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    const std::array candidates{
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedProviderId, .Path = kSharedProviderPath}},
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedConsumerId, .Path = kMissingQueryPath}},
    };
    const auto failed = manager->Load(candidates, {}, report);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().Code(), static_cast<unsigned>(ModuleError::kQuerySymbolMissing));
    EXPECT_EQ(failed.error().Domain(), kModuleErrorDomain);
    EXPECT_EQ(report.OriginalError, failed.error());
    EXPECT_EQ(report.Handle, ModuleHandle{});
    EXPECT_FALSE(manager->Find(kSharedProviderId));
    EXPECT_EQ(manager->Modules().size(), 2U);
    EXPECT_TRUE(manager->Load(std::span(candidates).first(1), {}, report));
    const std::array wrong{ModuleLoadCandidate{StaticModuleCandidate{BuiltinResidentQueries().front()}}};
    {
        const auto rejected = manager->Load(wrong, {}, report);
        EXPECT_FALSE(rejected);
        if (!rejected)
        {
            EXPECT_EQ(rejected.error(), MakeModuleError(ModuleError::kWrongLoadSource));
        }
    }
}

TEST(BatchLoad, RejectsNativeIdentityMismatchNullDefinitionAndDuplicateBeforeOpen)
{
    using namespace fixture;
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    for (const auto* path : {kSharedConsumerPath, kNullQueryPath})
    {
        const std::array candidates{
            ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = kSharedProviderId, .Path = path}},
        };
        const auto failed = manager->Load(candidates, {}, report);
        EXPECT_FALSE(failed);
        EXPECT_EQ(manager->Modules().size(), 2U);
    }
    const auto rootId = manager->Inspect(manager->Modules().front())->Descriptor.Id;
    const std::array duplicate{
        ModuleLoadCandidate{SharedModuleCandidate{.ExpectedId = rootId, .Path = "does-not-exist"}},
    };
    {
        const auto rejected = manager->Load(duplicate, {}, report);
        EXPECT_FALSE(rejected);
        if (!rejected)
        {
            EXPECT_EQ(rejected.error(), MakeModuleError(ModuleError::kDuplicateModule));
        }
    }
}
TEST(BatchLoad, FailedBatchKeepsPreviouslyActiveModuleUsable)
{
    using namespace fixture;
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    const std::array providerCandidate{
        ModuleLoadCandidate{SharedModuleCandidate{kSharedProviderId, kSharedProviderPath}},
    };
    auto loaded = manager->Load(providerCandidate, {}, report);
    ASSERT_TRUE(loaded);
    const auto provider = loaded->Modules.front();
    ASSERT_TRUE(manager->Register(provider, report));
    ASSERT_TRUE(manager->Activate(provider, report));
    const std::array failedCandidate{ModuleLoadCandidate{SharedModuleCandidate{kSharedConsumerId, kMissingQueryPath}}};
    EXPECT_FALSE(manager->Load(failedCandidate, {}, report));
    EXPECT_EQ(report.Handle, ModuleHandle{});
    EXPECT_EQ(manager->Inspect(provider)->State, ModuleState::kActive);
    EXPECT_FALSE(manager->Find(kSharedConsumerId));
    EXPECT_TRUE(manager->CreateScope(pg::service::Scope::kContext, manager->HostScope(), report));
}
namespace
{
constexpr ModuleId kConflictResident{.Bytes = {std::byte{0x63}}};
Status RegisterConflict(RegistrationContext& context) noexcept
{
    return context.Declare(fixture::kSharedProviderDeclaration, "resident-conflict");
}
const ModuleDefinition* QueryConflictResident() noexcept
{
    static constexpr ModuleDefinition kDefinition{
        .Descriptor = {.Id = kConflictResident, .Dependencies = {}, .DisplayName = {}, .BuildIdentity = {}},
        .RegisterModule = RegisterConflict,
    };
    return &kDefinition;
}
} // namespace

TEST(BatchLoad, NativeUnloadReentryPreservesCompletedReport)
{
    for (const bool failRegistration : {false, true})
    {
        auto& observer = pg::test::Observer();
        observer.Reset();
        RegistrationScope declarations;
        ModuleOperationReport report;
        std::vector<QueryModuleFn> queries(BuiltinResidentQueries().begin(), BuiltinResidentQueries().end());
        if (failRegistration)
        {
            queries.push_back(QueryConflictResident);
        }
        auto manager = ModuleManager::Create(declarations, queries, {}, report);
        ASSERT_TRUE(manager);
        if (failRegistration)
        {
            ASSERT_TRUE(manager->Register(manager->Find(kConflictResident).value_or(ModuleHandle{}), report));
        }
        const std::array candidates{
            ModuleLoadCandidate{SharedModuleCandidate{fixture::kSharedProviderId, fixture::kSharedProviderPath}},
        };
        const auto loaded = manager->Load(candidates, {}, report);
        ASSERT_TRUE(loaded);
        const auto handle = loaded->Modules.front();
        std::optional<Error> original;
        if (failRegistration)
        {
            const auto registered = manager->Register(handle, report);
            ASSERT_FALSE(registered);
            original = registered.error();
            ASSERT_EQ(original, MakeModuleError(ModuleError::kDeclarationConflict));
        }
        else
        {
            ASSERT_TRUE(manager->BeginUnload(handle, report));
        }
        struct Reentry
        {
            ModuleManager* Manager;
            ModuleOperationReport* Report;
            bool Called{};
            bool SawBusy{};
        };
        Reentry state{.Manager = &*manager, .Report = &report};
        observer.SetNativeUnloadCallback(
            [](void* context) noexcept
            {
                auto& reentry = *static_cast<Reentry*>(context);
                reentry.Called = true;
                const auto nested = reentry.Manager->BeginShutdown(*reentry.Report);
                reentry.SawBusy = !nested && nested.error() == MakeModuleError(ModuleError::kBusy);
            },
            &state);
        const auto finished = manager->TryFinishUnload(handle, report);
        observer.SetNativeUnloadCallback(nullptr, nullptr);
        ASSERT_TRUE(finished);
        EXPECT_TRUE(state.Called);
        EXPECT_TRUE(state.SawBusy);
        EXPECT_EQ(observer.NativeUnloadCount(), 1U);
        EXPECT_EQ(report.Progress, ModuleOperationProgress::kUnloaded);
        EXPECT_EQ(report.Module, fixture::kSharedProviderId);
        EXPECT_EQ(report.Handle, handle);
        EXPECT_EQ(report.PrimaryError, original);
        EXPECT_EQ(report.OriginalError, original);
        EXPECT_FALSE(report.CleanupError);
        EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kNone);
        EXPECT_FALSE(manager->Inspect(handle));
    }
}
#else
TEST(BatchLoad, MonolithicRejectsSharedSource)
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    const std::array candidates{ModuleLoadCandidate{SharedModuleCandidate{}}};
    {
        const auto rejected = manager->Load(candidates, {}, report);
        EXPECT_FALSE(rejected);
        if (!rejected)
        {
            EXPECT_EQ(rejected.error(), MakeModuleError(ModuleError::kWrongLoadSource));
        }
    }
}
#endif
} // namespace pg::module
