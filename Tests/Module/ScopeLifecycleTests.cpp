#include "Fixtures/ScopeLifecycleFixture.h"
#include <gtest/gtest.h>

#include "Error.h"
#include "Fixtures/ManagerFixture.h"
#include <ModuleBuiltin.h>
#include <ModuleDeclaration.h>
#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <ModuleHandle.h>
#include <ModuleIdentity.h>
#include <ModuleManager.h>
#include <ModuleOperation.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeAccess.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace pg::module;
using pg::service::Scope;
using namespace pg::test::scope_lifecycle;
constexpr ModuleId kProviderModule{.Bytes = {std::byte{2}}};
constexpr DeclarationId kProvider{.Bytes = {std::byte{2}}};
constexpr pg::Error kCleanupFailure{pg::ErrorDomainId::Constant<0x73636f7065746573ULL>(), 72};

struct Fixture
{
    Fixture() { Reset(); }
    ~Fixture() { Reset(); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    Fixture(Fixture&&) = delete;
    Fixture& operator=(Fixture&&) = delete;

    inline static std::shared_ptr<Counters> gState;
    inline static const ModuleDefinition* gFirst{};
    inline static const ModuleDefinition* gSecond{};
    inline static const ModuleDefinition* gThird{};
    static void Reset() noexcept
    {
        gState.reset();
        gFirst = nullptr;
        gSecond = nullptr;
        gThird = nullptr;
    }
    static const ModuleDefinition* QueryFirst() noexcept { return gFirst; }
    static const ModuleDefinition* QuerySecond() noexcept { return gSecond; }
    static const ModuleDefinition* QueryThird() noexcept { return gThird; }
    [[nodiscard]] static pg::Status Register(RegistrationContext& context) noexcept
    {
        return RegisterObjects(context, gState);
    }
    [[nodiscard]] static pg::Status RegisterProvider(RegistrationContext& context) noexcept
    {
        return context.DeclareFactory(
            kProvider, Scope::kHost,
            ScopeFactory(
                [state = gState](ScopeBuildContext& build) noexcept -> pg::Status
                {
                    auto access =
                        build.Own(std::make_unique<Object>(state, ++state->Made, true, false),
                                  ScopeHooks<Object>{.Start = Start, .RequestStop = Stop, .PollStopped = Poll});
                    return access ? pg::Status{} : std::unexpected(access.error());
                }));
    }
    [[nodiscard]] static pg::Status RegisterDependent(RegistrationContext& context) noexcept
    {
        constexpr std::array kDependencies{ScopeDependency{.Id = kProvider, .Scope = Scope::kHost}};
        return context.DeclareFactory(
            kObject, Scope::kContext,
            ScopeFactory(
                [state = gState](ScopeBuildContext& build) noexcept -> pg::Status
                {
                    if (state->WrongDependencyType)
                    {
                        [[maybe_unused]] const auto ignored = build.Dependency<double>(kProvider);
                        return {};
                    }
                    const auto provider = build.Dependency<Object>(kProvider);
                    if (!provider)
                    {
                        return std::unexpected(provider.error());
                    }
                    auto access =
                        build.Own(std::make_unique<Object>(state, (*provider)->Value + 10, false, true),
                                  ScopeHooks<Object>{.Start = Start, .RequestStop = Stop, .PollStopped = Poll});
                    if (!access)
                    {
                        return std::unexpected(access.error());
                    }
                    state->Exports.push_back({.Scope = build.Scope(), .Access = *access});
                    return {};
                }),
            kDependencies);
    }
};
constexpr std::array kEntries{StaticCatalogEntry{Fixture::QueryFirst}, StaticCatalogEntry{Fixture::QuerySecond}};

ModuleDefinition Definition(ModuleId id, RegisterModuleFn registration,
                            std::span<const ModuleDependency> dependencies = {})
{
    return {
        .Descriptor = {.Id = id, .Dependencies = dependencies, .DisplayName = {}, .BuildIdentity = {}},
        .RegisterModule = registration,
    };
}
[[nodiscard]] pg::Result<ModuleManager> Host(RegistrationScope& declarations, ModuleOperationReport& report,
                                             const std::shared_ptr<Counters>& state)
{
    Fixture::gState = state;
    const auto definition = Definition(kModule, Fixture::Register);
    Fixture::gFirst = &definition;
    return pg::module::test::CreateManager({std::span(kEntries).first<1>()}, declarations, {}, report);
}

TEST(ScopeLifecycle, ScopeFailureIsLocalAndFactoryRepeats)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(host->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->Activate(host->Find(kModule).value_or(ModuleHandle{}), report));
    const auto first = host->CreateScope(Scope::kContext, host->HostScope(), report);
    ASSERT_TRUE(first);
    ASSERT_EQ(state->Exports.size(), 1);
    state->FailFactoryAt = 2;
    const auto failed = host->CreateScope(Scope::kContext, host->HostScope(), report);
    const auto rejectedHandle = report.Scope;
    const auto snapshot = report;
    ASSERT_FALSE(failed);
    ASSERT_EQ(failed.error(), kFailure);
    ASSERT_EQ(state->Destroyed, 1);
    ASSERT_EQ(state->Registrations, 1);
    ASSERT_EQ(pg::module::test::BusinessSnapshots(*host).front().State, ModuleState::kActive);
    ASSERT_TRUE(state->Exports.front().Access.TryBorrow());
    const auto third = host->CreateScope(Scope::kContext, host->HostScope(), report);
    ASSERT_TRUE(third);
    ASSERT_EQ(third->Slot, rejectedHandle.Slot);
    ASSERT_NE(third->Generation, rejectedHandle.Generation);
    ASSERT_FALSE(host->BeginCloseScope(rejectedHandle, report));
    ASSERT_FALSE(state->Exports.at(1).Access.TryBorrow());
    ASSERT_TRUE(state->Exports.back().Access.TryBorrow());
    ASSERT_EQ(snapshot.OriginalError, kFailure);
    ASSERT_EQ(snapshot.Progress, ModuleOperationProgress::kRolledBack);
    ASSERT_EQ(snapshot.CleanupOwner, ModuleCleanupOwner::kNone);
    ASSERT_TRUE(host->BeginShutdown(report));
    ASSERT_TRUE(host->TryFinishShutdown(report));
    ASSERT_EQ(state->Destroyed, 3);
    ASSERT_TRUE(declarations.Declarations().empty());
}

TEST(ScopeLifecycle, BorrowAndWorkDelayDestructionIndependently)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(host->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->Activate(host->Find(kModule).value_or(ModuleHandle{}), report));
    const auto first = host->CreateScope(Scope::kContext, host->HostScope(), report);
    const auto second = host->CreateScope(Scope::kContext, host->HostScope(), report);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    auto borrow = state->Exports.front().Access.TryBorrow();
    state->Finished = false;
    state->BusyValue = 1;
    ASSERT_TRUE(borrow);
    ASSERT_TRUE(host->BeginCloseScope(*first, report));
    EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kCallerUnload);
    const auto busy = host->TryFinishCloseScope(*first, report);
    ASSERT_FALSE(busy);
    ASSERT_EQ(busy.error(), MakeModuleError(ModuleError::kBusy));
    EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kCallerUnload);
    ASSERT_EQ(state->Destroyed, 0);
    ASSERT_FALSE(state->Exports.front().Access.TryBorrow());
    ASSERT_TRUE(host->BeginCloseScope(*second, report));
    ASSERT_TRUE(host->TryFinishCloseScope(*second, report));
    EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kNone);
    ASSERT_EQ(state->Destroyed, 1);
    state->Finished = false;
    borrow->Release();
    ASSERT_FALSE(host->TryFinishCloseScope(*first, report));
    ASSERT_EQ(state->Destroyed, 1);
    state->Finished = true;
    ASSERT_TRUE(host->TryFinishCloseScope(*first, report));
    ASSERT_EQ(state->Destroyed, 2);
    ASSERT_EQ(state->Stopped, 2);
}

TEST(ScopeLifecycle, PartialStartFailureStaysCleanupOnlyAndReloadUsesNewTokens)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(host->Find(kModule).value_or(ModuleHandle{}), report));
    const auto first = host->CreateScope(Scope::kContext, host->HostScope(), report);
    const auto second = host->CreateScope(Scope::kContext, host->HostScope(), report);
    state->FailStartAt = 2;
    state->Finished = false;
    const auto activated = host->Activate(host->Find(kModule).value_or(ModuleHandle{}), report);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_FALSE(activated);
    ASSERT_EQ(activated.error(), kFailure);
    ASSERT_EQ(report.OriginalError, kFailure);
    ASSERT_EQ(pg::module::test::BusinessSnapshots(*host).front().State, ModuleState::kCleanupPending);
    ASSERT_EQ(state->Destroyed, 0);
    ASSERT_FALSE(host->Activate(host->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_FALSE(host->TryFinishUnload(host->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_EQ(report.OriginalError, kFailure);
    state->Finished = true;
    ASSERT_TRUE(host->TryFinishUnload(host->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_EQ(report.OriginalError, kFailure);
    ASSERT_EQ(state->Destroyed, 2);
    const auto reloadedDefinition = Definition(kModule, Fixture::Register);
    Fixture::gFirst = &reloadedDefinition;
    const std::array reloadCandidates{ModuleLoadCandidate{StaticModuleCandidate{Fixture::QueryFirst}}};
    ASSERT_TRUE(host->Load(reloadCandidates, {}, report));
    ASSERT_TRUE(host->Register(host->Find(kModule).value_or(ModuleHandle{}), report));
    state->FailStartAt = 0;
    ASSERT_TRUE(host->Activate(host->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_EQ(state->Registrations, 2);
    ASSERT_EQ(state->Exports.size(), 4);
    ASSERT_FALSE(state->Exports.front().Access.TryBorrow());
    ASSERT_TRUE(state->Exports.back().Access.TryBorrow());
    ASSERT_TRUE(host->BeginShutdown(report));
    ASSERT_TRUE(host->TryFinishShutdown(report));
    ASSERT_EQ(state->Destroyed, 4);
}

TEST(ScopeLifecycle, DependenciesPreserveParentAndReverseDestruction)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    Fixture::gState = state;
    constexpr std::array kDependencies{ModuleDependency{.Id = kProviderModule}};
    const auto first = Definition(kModule, Fixture::RegisterDependent, kDependencies);
    const auto second = Definition(kProviderModule, Fixture::RegisterProvider);
    Fixture::gFirst = &first;
    Fixture::gSecond = &second;
    auto host = pg::module::test::CreateManager({kEntries}, declarations, {}, report);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(host->Find(kProviderModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->Register(host->Find(kModule).value_or(ModuleHandle{}), report));
    const auto early = host->Activate(host->Find(kModule).value_or(ModuleHandle{}), report);
    ASSERT_FALSE(early);
    ASSERT_EQ(early.error(), MakeModuleError(ModuleError::kDependencyNotActive));
    ASSERT_EQ(state->Made, 0);
    ASSERT_TRUE(host->Activate(host->Find(kProviderModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->Activate(host->Find(kModule).value_or(ModuleHandle{}), report));
    const auto context = host->CreateScope(Scope::kContext, host->HostScope(), report);
    ASSERT_TRUE(context);
    ASSERT_TRUE(state->Exports.front().Access.TryBorrow());
    const auto providerUnload = host->BeginUnload(host->Find(kProviderModule).value_or(ModuleHandle{}), report);
    ASSERT_FALSE(providerUnload);
    ASSERT_EQ(providerUnload.error(), MakeModuleError(ModuleError::kDependencyInUse));
    ASSERT_TRUE(host->BeginShutdown(report));
    ASSERT_TRUE(host->TryFinishShutdown(report));
    ASSERT_FALSE(state->BrokenOrder);
    ASSERT_EQ(state->DestroyOrder, (std::vector<int>{11, 1}));
}

TEST(ScopeLifecycle, WrongDependencyTypeCannotCommit)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    state->WrongDependencyType = true;
    Fixture::gState = state;
    const auto first = Definition(kModule, Fixture::RegisterDependent);
    const auto second = Definition(kProviderModule, Fixture::RegisterProvider);
    Fixture::gFirst = &first;
    Fixture::gSecond = &second;
    auto host = pg::module::test::CreateManager({kEntries}, declarations, {}, report);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(host->Find(kProviderModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->Register(host->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->Activate(host->Find(kProviderModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->Activate(host->Find(kModule).value_or(ModuleHandle{}), report));
    const auto scope = host->CreateScope(Scope::kContext, host->HostScope(), report);
    ASSERT_FALSE(scope);
    ASSERT_EQ(scope.error(), MakeModuleError(ModuleError::kTypeMismatch));
    ASSERT_EQ(state->Made, 1);
    ASSERT_EQ(pg::module::test::BusinessSnapshots(*host).front().State, ModuleState::kActive);
    ASSERT_TRUE(host->PollCleanup(report));
}

TEST(ScopeLifecycle, HandlesAndMovingHostPreserveOwnership)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    RegistrationScope otherDeclarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto original = Host(declarations, report, state);
    auto foreign = Host(otherDeclarations, report, state);
    ASSERT_TRUE(original);
    ASSERT_TRUE(foreign);
    ASSERT_TRUE(original->Register(original->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(original->Activate(original->Find(kModule).value_or(ModuleHandle{}), report));
    auto moved = std::move(*original);
    ASSERT_FALSE(moved.CreateScope(Scope::kContext, foreign->HostScope(), report));
    ASSERT_FALSE(moved.CreateScope(Scope::kWorld, moved.HostScope(), report));
    const auto context = moved.CreateScope(Scope::kContext, moved.HostScope(), report);
    ASSERT_TRUE(context);
    ASSERT_TRUE(state->Exports.front().Access.TryBorrow());
    const auto world = moved.CreateScope(Scope::kWorld, *context, report);
    ASSERT_TRUE(world);
    ASSERT_TRUE(moved.BeginCloseScope(*context, report));
    ASSERT_TRUE(moved.TryFinishCloseScope(*context, report));
    ASSERT_FALSE(moved.BeginCloseScope(*world, report));
    ASSERT_TRUE(moved.BeginShutdown(report));
    ASSERT_TRUE(moved.TryFinishShutdown(report));
}

TEST(ScopeLifecycle, ManagementReentryAndAliasedReportDoNotCorruptOuterOperation)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(host->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->Activate(host->Find(kModule).value_or(ModuleHandle{}), report));
    state->Reenter = true;
    state->Host = &*host;
    state->SharedReport = &report;
    const auto scope = host->CreateScope(Scope::kContext, host->HostScope(), report);
    ASSERT_TRUE(scope);
    ASSERT_TRUE(state->SawBusy);
    ASSERT_FALSE(report.PrimaryError);
    ASSERT_EQ(report.Scope, *scope);
    ASSERT_EQ(report.Progress, ModuleOperationProgress::kScopeReady);
}

TEST(ScopeLifecycle, FailedScopeWithoutHandleRemainsTrackedUntilWorkStops)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(host->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->Activate(host->Find(kModule).value_or(ModuleHandle{}), report));
    EXPECT_FALSE(host->CreateScope(Scope::kWorld, host->HostScope(), report));
    EXPECT_EQ(report.PrimaryError, MakeModuleError(ModuleError::kInvalidScope));
    EXPECT_EQ(report.Progress, ModuleOperationProgress::kUnchanged);
    EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kNone);
    const auto first = host->CreateScope(Scope::kContext, host->HostScope(), report);
    ASSERT_TRUE(first);
    state->FailStartAt = 2;
    state->Finished = false;
    state->BusyValue = 2;
    {
        ModuleOperationReport failure;
        const auto failed = host->CreateScope(Scope::kContext, host->HostScope(), failure);
        EXPECT_FALSE(failed);
        if (!failed)
        {
            EXPECT_EQ(failed.error(), kFailure);
        }
        EXPECT_EQ(failure.OriginalError, kFailure);
        EXPECT_EQ(failure.Progress, ModuleOperationProgress::kCleanupPending);
        EXPECT_EQ(failure.CleanupOwner, ModuleCleanupOwner::kManagerCleanup);
        EXPECT_FALSE(host->TryFinishCloseScope(failure.Scope, report));
        EXPECT_EQ(report.PrimaryError, MakeModuleError(ModuleError::kScopeNotFound));
        EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kNone);
    }
    const auto busy = host->PollCleanup(report);
    state->Finished = true;
    ASSERT_FALSE(busy);
    ASSERT_EQ(report.OriginalError, kFailure);
    ASSERT_EQ(report.CleanupOwner, ModuleCleanupOwner::kManagerCleanup);
    ASSERT_TRUE(state->Exports.front().Access.TryBorrow());
    ASSERT_TRUE(host->PollCleanup(report));
    ASSERT_EQ(report.OriginalError, kFailure);
    ASSERT_EQ(report.CleanupOwner, ModuleCleanupOwner::kNone);
    ASSERT_EQ(state->Destroyed, 1);
    ASSERT_TRUE(host->BeginShutdown(report));
    ASSERT_TRUE(host->TryFinishShutdown(report));
    ASSERT_EQ(state->Destroyed, 2);
}

TEST(ScopeLifecycle, RejectsCombinedAndLocalCyclesBeforeFactories)
{
    const Fixture callbacks;
    const auto state = std::make_shared<Counters>();
    Fixture::gState = state;
    {
        RegistrationScope declarations;
        ModuleOperationReport report;
        constexpr std::array kModuleDependencies{ModuleDependency{.Id = kProviderModule}};
        const auto first = Definition(kModule, Fixture::Register, kModuleDependencies);
        const auto registerCycle = [](RegistrationContext& context) noexcept -> pg::Status
        {
            constexpr std::array kDependencies{ScopeDependency{.Id = kObject, .Scope = Scope::kContext}};
            return context.DeclareFactory(kProvider, Scope::kContext,
                                          ScopeFactory(
                                              [](ScopeBuildContext& build) noexcept -> pg::Status
                                              {
                                                  ++Fixture::gState->Made;
                                                  const auto value = build.Own(std::make_unique<int>(1));
                                                  return value ? pg::Status{} : std::unexpected(value.error());
                                              }),
                                          kDependencies);
        };
        const auto second = Definition(kProviderModule, registerCycle);
        Fixture::gFirst = &first;
        Fixture::gSecond = &second;
        auto host = pg::module::test::CreateManager({kEntries}, declarations, {}, report);
        ASSERT_TRUE(host);
        ASSERT_TRUE(host->Register(host->Find(kProviderModule).value_or(ModuleHandle{}), report));
        ASSERT_TRUE(host->Register(host->Find(kModule).value_or(ModuleHandle{}), report));
        const auto cycle = host->Activate(host->Find(kProviderModule).value_or(ModuleHandle{}), report);
        ASSERT_FALSE(cycle);
        ASSERT_EQ(cycle.error(), MakeModuleError(ModuleError::kDependencyCycle));
        ASSERT_EQ(state->Made, 0);
    }
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto registerLocal = [](RegistrationContext& context) noexcept -> pg::Status
    {
        for (const auto id : {kObject, kProvider})
        {
            const ScopeDependency dependency{.Id = id == kObject ? kProvider : kObject, .Scope = Scope::kContext};
            const std::array dependencies{dependency};
            const auto declared =
                context.DeclareFactory(id, Scope::kContext,
                                       ScopeFactory(
                                           [](ScopeBuildContext& build) noexcept -> pg::Status
                                           {
                                               ++Fixture::gState->Made;
                                               const auto value = build.Own(std::make_unique<int>(1));
                                               return value ? pg::Status{} : std::unexpected(value.error());
                                           }),
                                       dependencies);
            if (!declared)
            {
                return declared;
            }
        }
        return {};
    };
    const auto definition = Definition(kModule, registerLocal);
    Fixture::gFirst = &definition;
    auto host = pg::module::test::CreateManager({std::span(kEntries).first<1>()}, declarations, {}, report);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(host->Find(kModule).value_or(ModuleHandle{}), report));
    const auto cycle = host->Activate(host->Find(kModule).value_or(ModuleHandle{}), report);
    ASSERT_FALSE(cycle);
    ASSERT_EQ(cycle.error(), MakeModuleError(ModuleError::kDependencyCycle));
    ASSERT_EQ(state->Made, 0);
    ASSERT_EQ(pg::module::test::BusinessSnapshots(*host).front().State, ModuleState::kRegistered);
}

TEST(ScopeLifecycle, ScopeBudgetIsCheckedBeforeCallbacksAndSlotsCanBeReused)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    ASSERT_TRUE(host);
    std::vector<ScopeHandle> contexts;
    for (int index = 0; index < 255; ++index)
    {
        const auto scope = host->CreateScope(Scope::kContext, host->HostScope(), report);
        ASSERT_TRUE(scope);
        contexts.push_back(*scope);
    }
    const auto overflow = host->CreateScope(Scope::kContext, host->HostScope(), report);
    ASSERT_FALSE(overflow);
    ASSERT_EQ(overflow.error(), MakeModuleError(ModuleError::kLimitExceeded));
    ASSERT_EQ(state->Made, 0);
    ASSERT_TRUE(host->BeginCloseScope(contexts.front(), report));
    ASSERT_TRUE(host->TryFinishCloseScope(contexts.front(), report));
    const auto reused = host->CreateScope(Scope::kContext, host->HostScope(), report);
    ASSERT_TRUE(reused);
    ASSERT_EQ(reused->Slot, contexts.front().Slot);
    ASSERT_NE(reused->Generation, contexts.front().Generation);
    ASSERT_FALSE(host->BeginCloseScope(contexts.front(), report));
}

void ProviderWorkSurvivesBusyCleanup(bool indirect)
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    Fixture::gState = state;
    constexpr ModuleId kBridgeModule{.Bytes = {std::byte{3}}};
    const std::array dependencies{ModuleDependency{.Id = indirect ? kBridgeModule : kProviderModule}};
    constexpr std::array kProviderDependencies{ModuleDependency{.Id = kProviderModule}};
    const auto registration = [](RegistrationContext& context) noexcept -> pg::Status
    {
        return context.DeclareFactory(
            kObject, Scope::kHost,
            ScopeFactory(
                [state = Fixture::gState](ScopeBuildContext& build) noexcept -> pg::Status
                {
                    const auto access =
                        build.Own(std::make_unique<Object>(state, ++state->Made, false, true),
                                  ScopeHooks<Object>{.Start = Start, .RequestStop = Stop, .PollStopped = Poll});
                    return access ? pg::Status{} : std::unexpected(access.error());
                }));
    };
    const auto first = Definition(kModule, registration, dependencies);
    const auto second = Definition(kProviderModule, Fixture::RegisterProvider);
    const auto bridge = Definition(
        kBridgeModule, [](RegistrationContext&) noexcept -> pg::Status { return {}; }, kProviderDependencies);
    Fixture::gFirst = &first;
    Fixture::gSecond = &second;
    Fixture::gThird = &bridge;
    const std::array entries{
        StaticCatalogEntry{Fixture::QueryFirst},
        StaticCatalogEntry{Fixture::QuerySecond},
        StaticCatalogEntry{Fixture::QueryThird},
    };
    const std::span<const StaticCatalogEntry> selected = indirect ? std::span(entries) : std::span(entries).first(2);
    auto host = pg::module::test::CreateManager({selected}, declarations, {}, report);
    ASSERT_TRUE(host);
    for (const auto id : pg::module::test::BusinessOrder(*host))
    {
        ASSERT_TRUE(host->Register(host->Find(id).value_or(ModuleHandle{}), report));
        ASSERT_TRUE(host->Activate(host->Find(id).value_or(ModuleHandle{}), report));
    }
    state->Finished = false;
    state->BusyValue = 2;
    if (!host->BeginShutdown(report))
    {
        state->Finished = true;
        FAIL() << "BeginShutdown 失败";
    }
    const auto busy = host->TryFinishShutdown(report);
    const bool retained = !busy && busy.error() == MakeModuleError(ModuleError::kBusy) && state->Destroyed == 0 &&
                          state->Stopped == 1 && state->ProviderAlive;
    state->Finished = true;
    const auto finished = host->TryFinishShutdown(report);
    ASSERT_TRUE(retained);
    ASSERT_TRUE(finished);
    ASSERT_FALSE(state->BrokenOrder);
    ASSERT_EQ(state->DestroyOrder, (std::vector<int>{2, 1}));
}

TEST(ScopeLifecycle, DirectModuleDependencyKeepsProviderWorkAliveDuringBusyCleanup)
{
    const Fixture callbacks;
    ProviderWorkSurvivesBusyCleanup(false);
}

TEST(ScopeLifecycle, TransitiveModuleDependencyKeepsProviderWorkAliveDuringBusyCleanup)
{
    const Fixture callbacks;
    ProviderWorkSurvivesBusyCleanup(true);
}

TEST(ScopeLifecycle, ShutdownRetainsUnreturnedScopeFailure)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(host->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->Activate(host->Find(kModule).value_or(ModuleHandle{}), report));
    state->FailStartAt = 1;
    state->Finished = false;
    const auto failed = host->CreateScope(Scope::kContext, host->HostScope(), report);
    const auto original = report.OriginalError;
    if (!host->BeginShutdown(report))
    {
        state->Finished = true;
        FAIL() << "BeginShutdown 失败";
    }
    const auto busy = host->TryFinishShutdown(report);
    const bool retained = !failed && original == kFailure && !busy && report.OriginalError == kFailure &&
                          report.PrimaryError == kFailure && report.CleanupError == MakeModuleError(ModuleError::kBusy);
    state->Finished = true;
    const auto done = host->TryFinishShutdown(report);
    ASSERT_TRUE(retained);
    ASSERT_TRUE(done);
    ASSERT_EQ(report.OriginalError, kFailure);
    ASSERT_EQ(state->Destroyed, 1);
}

struct RetireCallback
{
    std::shared_ptr<Counters> State;
    explicit RetireCallback(std::shared_ptr<Counters> state)
        : State(std::move(state))
    {
    }
    ~RetireCallback()
    {
        const auto result =
            State->Host->Register(State->Host->Find(kProviderModule).value_or(ModuleHandle{}), *State->SharedReport);
        State->SawBusy = !result && result.error() == MakeModuleError(ModuleError::kBusy);
    }
    RetireCallback(const RetireCallback&) = delete;
    RetireCallback& operator=(const RetireCallback&) = delete;
    RetireCallback(RetireCallback&&) = delete;
    RetireCallback& operator=(RetireCallback&&) = delete;
};

[[nodiscard]] pg::Status DeclareRetiringFactory(RegistrationContext& context) noexcept
{
    return context.DeclareFactory(
        kObject, Scope::kContext,
        ScopeFactory(
            [guard = std::make_unique<RetireCallback>(Fixture::gState)](ScopeBuildContext& build) noexcept -> pg::Status
            {
                [[maybe_unused]] const auto* retained = guard.get();
                const auto object = build.Own(std::make_unique<int>(1));
                return object ? pg::Status{} : std::unexpected(object.error());
            }));
}

TEST(ScopeLifecycle, FactoryRetirementBlocksSharedDeclarationReentry)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    Fixture::gState = state;
    const auto metadata = [](RegistrationContext& context) noexcept -> pg::Status
    { return context.Declare(kProvider, "nested metadata"); };
    const auto first = Definition(kModule, DeclareRetiringFactory);
    Fixture::gFirst = &first;
    auto host = pg::module::test::CreateManager({std::span(kEntries).first<1>()}, declarations, {}, report);
    const auto second = Definition(kProviderModule, metadata);
    Fixture::gFirst = &second;
    auto other = pg::module::test::CreateManager({std::span(kEntries).first<1>()}, declarations, {}, report);
    ASSERT_TRUE(host);
    ASSERT_TRUE(other);
    state->Host = &*other;
    state->SharedReport = &report;
    ASSERT_TRUE(host->Register(host->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->BeginUnload(host->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->TryFinishUnload(host->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(state->SawBusy);
    ASSERT_EQ(pg::module::test::BusinessSnapshots(*other).front().State, ModuleState::kLoaded);
    ASSERT_TRUE(declarations.Declarations().empty());
    ASSERT_EQ(report.Module, kModule);
}

TEST(ScopeLifecycle, FailedRegistrationRetiresFactoriesBeforeReporting)
{
    const Fixture callbacks;
    const std::array cases{
        std::pair{+[](RegistrationContext& context) noexcept -> pg::Status
                  {
                      if (const auto declared = DeclareRetiringFactory(context); !declared)
                      {
                          return declared;
                      }
                      return std::unexpected(kFailure);
                  },
                  kFailure},
        std::pair{+[](RegistrationContext& context) noexcept -> pg::Status
                  {
                      if (const auto declared = DeclareRetiringFactory(context); !declared)
                      {
                          return declared;
                      }
                      [[maybe_unused]] const auto ignored = context.Declare(kObject, "duplicate");
                      return {};
                  },
                  MakeModuleError(ModuleError::kDeclarationConflict)},
    };
    for (const auto& [callback, expected] : cases)
    {
        RegistrationScope declarations;
        ModuleOperationReport report;
        const auto state = std::make_shared<Counters>();
        Fixture::gState = state;
        const auto first = Definition(kModule, callback);
        Fixture::gFirst = &first;
        auto host = pg::module::test::CreateManager({std::span(kEntries).first<1>()}, declarations, {}, report);
        const auto metadata = [](RegistrationContext& context) noexcept -> pg::Status
        { return context.Declare(kProvider, "nested metadata"); };
        const auto second = Definition(kProviderModule, metadata);
        Fixture::gFirst = &second;
        auto other = pg::module::test::CreateManager({std::span(kEntries).first<1>()}, declarations, {}, report);
        ASSERT_TRUE(host);
        ASSERT_TRUE(other);
        state->Host = &*other;
        state->SharedReport = &report;
        const auto failed = host->Register(host->Find(kModule).value_or(ModuleHandle{}), report);
        ASSERT_FALSE(failed);
        ASSERT_EQ(failed.error(), expected);
        ASSERT_TRUE(state->SawBusy);
        ASSERT_EQ(report.Module, kModule);
        ASSERT_EQ(report.RelatedModule, ModuleId());
        ASSERT_EQ(report.Progress, ModuleOperationProgress::kCleanupPending);
        ASSERT_EQ(report.PrimaryError, expected);
        ASSERT_EQ(report.OriginalError, expected);
        ASSERT_FALSE(report.CleanupError);
        ASSERT_EQ(pg::module::test::BusinessSnapshots(*host).front().State, ModuleState::kCleanupPending);
        ASSERT_EQ(pg::module::test::BusinessSnapshots(*other).front().State, ModuleState::kLoaded);
        ASSERT_TRUE(declarations.Declarations().empty());
        ASSERT_TRUE(other->Register(other->Find(kProviderModule).value_or(ModuleHandle{}), report));
    }
}

TEST(ScopeLifecycle, ReportSeparatesOriginalFailureFromCleanupResult)
{
    const Fixture callbacks;
    for (int mode = 0; mode < 3; ++mode)
    {
        SCOPED_TRACE(mode);
        RegistrationScope declarations;
        ModuleOperationReport report;
        const auto state = std::make_shared<Counters>();
        auto host = Host(declarations, report, state);
        ASSERT_TRUE(host);
        ASSERT_TRUE(host->Register(host->Find(kModule).value_or(ModuleHandle{}), report));
        ASSERT_TRUE(host->Activate(host->Find(kModule).value_or(ModuleHandle{}), report));
        if (mode == 0)
        {
            state->FailFactoryAt = 1;
        }
        else
        {
            state->FailStartAt = 1;
            state->Finished = false;
        }
        if (mode == 2)
        {
            state->PollError = kCleanupFailure;
        }
        const auto failed = host->CreateScope(Scope::kContext, host->HostScope(), report);
        const auto snapshot = report;
        state->Finished = true;
        state->PollError.reset();
        ASSERT_TRUE(host->PollCleanup(report));
        const std::optional<pg::Error> expected =
            mode == 0 ? std::nullopt : std::optional(mode == 1 ? MakeModuleError(ModuleError::kBusy) : kCleanupFailure);
        ASSERT_FALSE(failed);
        ASSERT_EQ(failed.error(), kFailure);
        ASSERT_EQ(snapshot.PrimaryError, kFailure);
        ASSERT_EQ(snapshot.OriginalError, kFailure);
        ASSERT_EQ(snapshot.CleanupError, expected);
    }
}

TEST(ScopeLifecycle, ActivationReportSeparatesCleanupError)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(host->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->CreateScope(Scope::kContext, host->HostScope(), report));
    state->FailStartAt = 1;
    state->PollError = kCleanupFailure;
    const auto failed = host->Activate(host->Find(kModule).value_or(ModuleHandle{}), report);
    const auto snapshot = report;
    state->PollError.reset();
    state->Finished = true;
    ASSERT_FALSE(failed);
    ASSERT_EQ(failed.error(), kFailure);
    ASSERT_EQ(snapshot.PrimaryError, kFailure);
    ASSERT_EQ(snapshot.OriginalError, kFailure);
    ASSERT_EQ(snapshot.CleanupError, kCleanupFailure);
    ASSERT_TRUE(host->TryFinishUnload(host->Find(kModule).value_or(ModuleHandle{}), report));
}

TEST(ScopeLifecycle, ShutdownCauseSurvivesRetiringFailedScopeAcrossPolls)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(host->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->Activate(host->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->CreateScope(Scope::kContext, host->HostScope(), report));
    auto held = state->Exports.front().Access.TryBorrow();
    ASSERT_TRUE(held);
    state->FailStartAt = 2;
    state->Finished = false;
    state->BusyValue = 2;
    const auto failed = host->CreateScope(Scope::kContext, host->HostScope(), report);
    state->Finished = true;
    ASSERT_FALSE(failed);
    ASSERT_TRUE(host->BeginShutdown(report));
    const auto first = host->TryFinishShutdown(report);
    const auto firstReport = report;
    const auto second = host->TryFinishShutdown(report);
    const auto secondReport = report;
    held->Release();
    const auto final = host->TryFinishShutdown(report);
    ASSERT_FALSE(first);
    ASSERT_FALSE(second);
    ASSERT_TRUE(final);
    ASSERT_EQ(firstReport.OriginalError, kFailure);
    ASSERT_EQ(secondReport.OriginalError, kFailure);
    ASSERT_EQ(report.OriginalError, kFailure);
    ASSERT_EQ(state->Destroyed, 2);
}

TEST(ScopeLifecycle, ShutdownDoesNotImportCompletedHistoricalFailure)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(host->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->CreateScope(Scope::kContext, host->HostScope(), report));
    state->FailStartAt = 1;
    const auto failed = host->Activate(host->Find(kModule).value_or(ModuleHandle{}), report);
    ASSERT_FALSE(failed);
    ASSERT_TRUE(host->TryFinishUnload(host->Find(kModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->BeginShutdown(report));
    ASSERT_TRUE(host->TryFinishShutdown(report));
    ASSERT_FALSE(report.OriginalError);
    ASSERT_FALSE(report.PrimaryError);
}

TEST(ScopeLifecycle, InactiveDeclarationCycleDoesNotBlockUnrelatedActiveScope)
{
    const Fixture callbacks;
    constexpr ModuleId kThirdModule{.Bytes = {std::byte{3}}};
    const auto state = std::make_shared<Counters>();
    Fixture::gState = state;
    RegistrationScope declarations;
    ModuleOperationReport report;
    constexpr std::array kDependencies{ModuleDependency{.Id = kProviderModule}};
    const auto first = Definition(kModule, Fixture::Register, kDependencies);
    const auto cycleRegistration = [](RegistrationContext& context) noexcept -> pg::Status
    {
        constexpr std::array kDependencies{ScopeDependency{.Id = kObject, .Scope = Scope::kContext}};
        return context.DeclareFactory(kProvider, Scope::kContext,
                                      ScopeFactory(
                                          [](ScopeBuildContext& build) noexcept -> pg::Status
                                          {
                                              const auto object = build.Own(std::make_unique<int>(1));
                                              return object ? pg::Status{} : std::unexpected(object.error());
                                          }),
                                      kDependencies);
    };
    const auto independent = [](RegistrationContext& context) noexcept -> pg::Status
    {
        constexpr DeclarationId kThirdDeclaration{.Bytes = {std::byte{3}}};
        return context.DeclareFactory(kThirdDeclaration, Scope::kContext,
                                      ScopeFactory(
                                          [state = Fixture::gState](ScopeBuildContext& build) noexcept -> pg::Status
                                          {
                                              ++state->Made;
                                              const auto object = build.Own(std::make_unique<int>(19));
                                              return object ? pg::Status{} : std::unexpected(object.error());
                                          }));
    };
    const auto second = Definition(kProviderModule, cycleRegistration);
    const auto third = Definition(kThirdModule, independent);
    Fixture::gFirst = &first;
    Fixture::gSecond = &second;
    Fixture::gThird = &third;
    const std::array entries{
        StaticCatalogEntry{Fixture::QueryFirst},
        StaticCatalogEntry{Fixture::QuerySecond},
        StaticCatalogEntry{Fixture::QueryThird},
    };
    auto host = pg::module::test::CreateManager({entries}, declarations, {}, report);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(host->Find(kThirdModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->Activate(host->Find(kThirdModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->Register(host->Find(kProviderModule).value_or(ModuleHandle{}), report));
    ASSERT_TRUE(host->Register(host->Find(kModule).value_or(ModuleHandle{}), report));
    const auto rejected = host->Activate(host->Find(kProviderModule).value_or(ModuleHandle{}), report);
    ASSERT_FALSE(rejected);
    ASSERT_EQ(rejected.error(), MakeModuleError(ModuleError::kDependencyCycle));
    const auto scope = host->CreateScope(Scope::kContext, host->HostScope(), report);
    ASSERT_TRUE(scope);
    ASSERT_EQ(state->Made, 1);
    ASSERT_EQ(pg::module::test::BusinessSnapshots(*host).back().State, ModuleState::kActive);
}

TEST(ScopeLifecycle, FactoryMustOwnExactlyOneInactiveObject)
{
    const Fixture callbacks;
    const std::array<RegisterModuleFn, 2> registrations{
        [](RegistrationContext& context) noexcept -> pg::Status
        {
            return context.DeclareFactory(
                kObject, Scope::kHost,
                ScopeFactory([](ScopeBuildContext& /*build*/) noexcept -> pg::Status { return {}; }));
        },
        [](RegistrationContext& context) noexcept -> pg::Status
        {
            return context.DeclareFactory(
                kObject, Scope::kHost,
                ScopeFactory(
                    [state = Fixture::gState](ScopeBuildContext& build) noexcept -> pg::Status
                    {
                        const auto first = build.Own(std::make_unique<Object>(state, ++state->Made, false, false),
                                                     ScopeHooks<Object>{.Start = Start});
                        if (!first)
                        {
                            return std::unexpected(first.error());
                        }
                        state->Exports.push_back({.Scope = build.Scope(), .Access = *first});
                        [[maybe_unused]] const auto ignored =
                            build.Own(std::make_unique<Object>(state, ++state->Made, false, false));
                        return {};
                    }));
        },
    };
    for (const auto registration : registrations)
    {
        RegistrationScope declarations;
        ModuleOperationReport report;
        const auto state = std::make_shared<Counters>();
        Fixture::gState = state;
        const auto definition = Definition(kModule, registration);
        Fixture::gFirst = &definition;
        auto host = pg::module::test::CreateManager({std::span(kEntries).first<1>()}, declarations, {}, report);
        ASSERT_TRUE(host);
        ASSERT_TRUE(host->Register(host->Find(kModule).value_or(ModuleHandle{}), report));
        const auto failed = host->Activate(host->Find(kModule).value_or(ModuleHandle{}), report);
        ASSERT_FALSE(failed);
        ASSERT_EQ(failed.error(), MakeModuleError(ModuleError::kInvalidDeclaration));
        ASSERT_EQ(state->Started, 0);
        ASSERT_EQ(state->Destroyed, state->Made);
        ASSERT_FALSE((!state->Exports.empty() && state->Exports.front().Access.TryBorrow()));
        ASSERT_TRUE(host->TryFinishUnload(host->Find(kModule).value_or(ModuleHandle{}), report));
        ASSERT_TRUE(declarations.Declarations().empty());
    }
}

TEST(ScopeLifecycle, IncrementalLoadAndMoveKeepExistingScopeAccessStable)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto manager = Host(declarations, report, state);
    ASSERT_TRUE(manager);
    const auto handle = *manager->Find(kModule);
    ASSERT_TRUE(manager->Register(handle, report));
    ASSERT_TRUE(manager->Activate(handle, report));
    auto context = manager->CreateScope(Scope::kContext, manager->HostScope(), report);
    ASSERT_TRUE(context);
    auto borrowed = state->Exports.front().Access.TryBorrow();
    ASSERT_TRUE(borrowed);
    const auto second = Definition(kProviderModule, [](RegistrationContext&) noexcept -> pg::Status { return {}; });
    Fixture::gSecond = &second;
    const std::array candidates{ModuleLoadCandidate{StaticModuleCandidate{Fixture::QuerySecond}}};
    const auto loaded = manager->Load(candidates, {}, report);
    ASSERT_TRUE(loaded);
    auto moved = std::move(*manager);
    EXPECT_TRUE(moved.Inspect(handle));
    EXPECT_TRUE(state->Exports.front().Access.TryBorrow());
    EXPECT_EQ(state->Exports.front().Scope, *context);
    borrowed = std::unexpected(MakeModuleError(ModuleError::kNotReady));
}

TEST(ScopeLifecycle, ShutdownConsumerParentDoesNotWaitForProviderChildScope)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    Fixture::gState = state;
    const auto providerRegistration = [](RegistrationContext& context) noexcept -> pg::Status
    {
        return context.DeclareFactory(
            kProvider, Scope::kContext,
            ScopeFactory(
                [state = Fixture::gState](ScopeBuildContext& build) noexcept -> pg::Status
                {
                    auto access =
                        build.Own(std::make_unique<Object>(state, ++state->Made, true, false),
                                  ScopeHooks<Object>{.Start = Start, .RequestStop = Stop, .PollStopped = Poll});
                    return access ? pg::Status{} : std::unexpected(access.error());
                }));
    };
    const auto consumerRegistration = [](RegistrationContext& context) noexcept -> pg::Status
    {
        return context.DeclareFactory(
            kObject, Scope::kHost,
            ScopeFactory(
                [state = Fixture::gState](ScopeBuildContext& build) noexcept -> pg::Status
                {
                    auto access =
                        build.Own(std::make_unique<Object>(state, ++state->Made, false, true),
                                  ScopeHooks<Object>{.Start = Start, .RequestStop = Stop, .PollStopped = Poll});
                    return access ? pg::Status{} : std::unexpected(access.error());
                }));
    };
    const std::array dependencies{ModuleDependency{.Id = kProviderModule}};
    const auto first = Definition(kModule, consumerRegistration, dependencies);
    const auto second = Definition(kProviderModule, providerRegistration);
    Fixture::gFirst = &first;
    Fixture::gSecond = &second;
    auto manager = pg::module::test::CreateManager({kEntries}, declarations, {}, report);
    ASSERT_TRUE(manager);
    const auto provider = *manager->Find(kProviderModule);
    const auto consumer = *manager->Find(kModule);
    ASSERT_TRUE(manager->Register(provider, report));
    ASSERT_TRUE(manager->Activate(provider, report));
    ASSERT_TRUE(manager->CreateScope(Scope::kContext, manager->HostScope(), report));
    ASSERT_TRUE(manager->Register(consumer, report));
    ASSERT_TRUE(manager->Activate(consumer, report));
    ASSERT_TRUE(manager->BeginShutdown(report));
    ASSERT_TRUE(manager->TryFinishShutdown(report));
    EXPECT_FALSE(state->BrokenOrder);
    EXPECT_EQ(state->DestroyOrder, (std::vector<int>{2, 1}));
}

TEST(ScopeLifecycle, ShutdownRetainsResidentDeclarationsWhileBusinessIsBusy)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    Fixture::gState = state;
    const auto business = Definition(kModule, Fixture::Register);
    const auto resident = Definition(kProviderModule, [](RegistrationContext& context) noexcept -> pg::Status
                                     { return context.Declare(kProvider, "resident"); });
    Fixture::gFirst = &business;
    Fixture::gSecond = &resident;
    std::vector<QueryModuleFn> queries(BuiltinResidentQueries().begin(), BuiltinResidentQueries().end());
    queries.push_back(Fixture::QuerySecond);
    auto manager = ModuleManager::Create(declarations, queries, {}, report);
    ASSERT_TRUE(manager);
    const auto root = *manager->Find(kProviderModule);
    ASSERT_TRUE(manager->Register(root, report));
    ASSERT_TRUE(manager->Activate(root, report));
    const std::array candidates{ModuleLoadCandidate{StaticModuleCandidate{Fixture::QueryFirst}}};
    auto loaded = manager->Load(candidates, {}, report);
    ASSERT_TRUE(loaded);
    const auto handle = loaded->Modules.front();
    ASSERT_TRUE(manager->Register(handle, report));
    ASSERT_TRUE(manager->Activate(handle, report));
    const auto scope = manager->CreateScope(Scope::kContext, manager->HostScope(), report);
    ASSERT_TRUE(scope);
    state->Finished = false;
    ASSERT_TRUE(manager->BeginShutdown(report));
    EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kManagerShutdown);
    EXPECT_TRUE(manager->BeginCloseScope(*scope, report));
    EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kManagerShutdown);
    EXPECT_FALSE(manager->TryFinishCloseScope(*scope, report));
    EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kManagerShutdown);
    const auto pending = manager->TryFinishShutdown(report);
    EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kManagerShutdown);
    const bool residentPreserved = std::ranges::any_of(declarations.Declarations(), [](const auto& declaration)
                                                       { return declaration.Id == kProvider; });
    state->Finished = true;
    ASSERT_TRUE(manager->TryFinishShutdown(report));
    EXPECT_FALSE(pending);
    EXPECT_TRUE(residentPreserved);
}

TEST(ScopeLifecycle, ResidentConsumerDrainsBeforeBusinessProviderAndConfirmsLast)
{
    EXPECT_EXIT(
        {
            const Fixture callbacks;
            RegistrationScope declarations;
            ModuleOperationReport report;
            const auto state = std::make_shared<Counters>();
            Fixture::gState = state;
            const auto resident = Definition(kModule, Fixture::RegisterDependent);
            const auto business = Definition(kProviderModule, Fixture::RegisterProvider);
            Fixture::gFirst = &resident;
            Fixture::gSecond = &business;
            std::vector<QueryModuleFn> queries(BuiltinResidentQueries().begin(), BuiltinResidentQueries().end());
            queries.push_back(Fixture::QueryFirst);
            auto manager = ModuleManager::Create(declarations, queries, {}, report);
            if (!manager)
            {
                std::_Exit(90);
            }
            const std::array candidates{ModuleLoadCandidate{StaticModuleCandidate{Fixture::QuerySecond}}};
            auto loaded = manager->Load(candidates, {}, report);
            if (!loaded)
            {
                std::_Exit(90);
            }
            const auto provider = loaded->Modules.front();
            const auto consumer = manager->Find(kModule).value_or(ModuleHandle{});
            if (!manager->Register(provider, report) || !manager->Activate(provider, report) ||
                !manager->CreateScope(Scope::kContext, manager->HostScope(), report) ||
                !manager->Register(consumer, report) || !manager->Activate(consumer, report) ||
                !manager->BeginShutdown(report))
            {
                std::_Exit(90);
            }
            const auto finished = manager->PollCleanup(report);
            if (!finished)
            {
                std::_Exit(91);
            }
            if (!manager->Inspect(consumer) || manager->Inspect(provider) || state->BrokenOrder ||
                state->DestroyOrder != std::vector<int>({11, 1}))
            {
                std::_Exit(92);
            }
            if (!manager->TryFinishShutdown(report) || manager->Inspect(consumer))
            {
                std::_Exit(93);
            }
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

TEST(ScopeLifecycle, ModuleUnloadPollPreservesUnreturnedScopeFailureOnlyUntilCompletion)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto manager = Host(declarations, report, state);
    ASSERT_TRUE(manager);
    const auto module = manager->Find(kModule).value_or(ModuleHandle{});
    ASSERT_TRUE(manager->Register(module, report));
    ASSERT_TRUE(manager->Activate(module, report));
    state->FailStartAt = 1;
    state->Finished = false;
    {
        ModuleOperationReport failedScopeReport;
        const auto failed = manager->CreateScope(Scope::kContext, manager->HostScope(), failedScopeReport);
        const auto unload = manager->BeginUnload(module, failedScopeReport);
        state->Finished = true;
        ASSERT_FALSE(failed);
        ASSERT_EQ(failed.error(), kFailure);
        ASSERT_TRUE(unload);
    }
    report = {};
    ASSERT_TRUE(manager->PollCleanup(report));
    EXPECT_EQ(report.OriginalError, kFailure);
    EXPECT_EQ(report.PrimaryError, kFailure);
    EXPECT_FALSE(report.CleanupError);
    EXPECT_FALSE(manager->Inspect(module));
    EXPECT_EQ(state->Destroyed, 1);
    ASSERT_TRUE(manager->BeginShutdown(report));
    EXPECT_FALSE(report.OriginalError);
    ASSERT_TRUE(manager->TryFinishShutdown(report));
    EXPECT_FALSE(report.OriginalError);
}

TEST(ScopeLifecycle, IndirectResidentConsumerDrainsBeforeBusinessProviderAndConfirmsLast)
{
    EXPECT_EXIT(
        {
            const Fixture callbacks;
            RegistrationScope declarations;
            ModuleOperationReport report;
            const auto state = std::make_shared<Counters>();
            Fixture::gState = state;
            constexpr ModuleId kRootConsumer{.Bytes = {std::byte{4}}};
            const auto bridge = Definition(kModule, Fixture::RegisterDependent);
            const auto business = Definition(kProviderModule, Fixture::RegisterProvider);
            const auto root = Definition(
                kRootConsumer,
                [](RegistrationContext& context) noexcept -> pg::Status
                {
                    constexpr DeclarationId kRootObject{.Bytes = {std::byte{4}}};
                    constexpr std::array kDependencies{ScopeDependency{.Id = kObject, .Scope = Scope::kContext}};
                    return context.DeclareFactory(
                        kRootObject, Scope::kContext,
                        ScopeFactory(
                            [state = Fixture::gState](ScopeBuildContext& build) noexcept -> pg::Status
                            {
                                const auto provider = build.Dependency<Object>(kObject);
                                if (!provider)
                                {
                                    return std::unexpected(provider.error());
                                }
                                const auto access = build.Own(
                                    std::make_unique<Object>(state, (*provider)->Value + 10, false, true),
                                    ScopeHooks<Object>{.Start = Start, .RequestStop = Stop, .PollStopped = Poll});
                                return access ? pg::Status{} : std::unexpected(access.error());
                            }),
                        kDependencies);
                });
            Fixture::gFirst = &bridge;
            Fixture::gSecond = &business;
            Fixture::gThird = &root;
            std::vector<QueryModuleFn> queries(BuiltinResidentQueries().begin(), BuiltinResidentQueries().end());
            queries.push_back(Fixture::QueryFirst);
            queries.push_back(Fixture::QueryThird);
            auto manager = ModuleManager::Create(declarations, queries, {}, report);
            if (!manager)
            {
                std::_Exit(90);
            }
            const std::array candidates{ModuleLoadCandidate{StaticModuleCandidate{Fixture::QuerySecond}}};
            auto loaded = manager->Load(candidates, {}, report);
            if (!loaded)
            {
                std::_Exit(90);
            }
            const auto provider = loaded->Modules.front();
            const auto middle = manager->Find(kModule).value_or(ModuleHandle{});
            const auto consumer = manager->Find(kRootConsumer).value_or(ModuleHandle{});
            if (!manager->Register(provider, report) || !manager->Activate(provider, report) ||
                !manager->CreateScope(Scope::kContext, manager->HostScope(), report) ||
                !manager->Register(middle, report) || !manager->Activate(middle, report) ||
                !manager->Register(consumer, report) || !manager->Activate(consumer, report) ||
                !manager->BeginShutdown(report))
            {
                std::_Exit(90);
            }
            if (!manager->PollCleanup(report))
            {
                std::_Exit(91);
            }
            if (!manager->Inspect(middle) || !manager->Inspect(consumer) || manager->Inspect(provider) ||
                state->BrokenOrder || state->DestroyOrder != std::vector<int>({21, 11, 1}))
            {
                std::_Exit(92);
            }
            if (!manager->TryFinishShutdown(report) || manager->Inspect(middle) || manager->Inspect(consumer))
            {
                std::_Exit(93);
            }
            std::_Exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

} // namespace
