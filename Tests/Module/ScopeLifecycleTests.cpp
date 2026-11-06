#include "Fixtures/ScopeLifecycleFixture.h"
#include <gtest/gtest.h>

#include "Error.h"
#include <ModuleDeclaration.h>
#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <ModuleIdentity.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeAccess.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>
#include <StaticModuleHost.h>

#include <array>
#include <cstddef>
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
[[nodiscard]] pg::Result<StaticModuleHost> Host(RegistrationScope& declarations, ModuleOperationReport& report,
                                                const std::shared_ptr<Counters>& state)
{
    Fixture::gState = state;
    const auto definition = Definition(kModule, Fixture::Register);
    Fixture::gFirst = &definition;
    return StaticModuleHost::Create({std::span(kEntries).first<1>()}, declarations, {}, report);
}

TEST(ScopeLifecycle, ScopeFailureIsLocalAndFactoryRepeats)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(kModule, report));
    ASSERT_TRUE(host->Activate(kModule, report));
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
    ASSERT_EQ(host->Modules().front().State, StaticModuleState::kActive);
    ASSERT_TRUE(state->Exports.front().Access.TryBorrow());
    const auto third = host->CreateScope(Scope::kContext, host->HostScope(), report);
    ASSERT_TRUE(third);
    ASSERT_EQ(third->Slot, rejectedHandle.Slot);
    ASSERT_NE(third->Generation, rejectedHandle.Generation);
    ASSERT_FALSE(host->BeginCloseScope(rejectedHandle, report));
    ASSERT_FALSE(state->Exports.at(1).Access.TryBorrow());
    ASSERT_TRUE(state->Exports.back().Access.TryBorrow());
    ASSERT_EQ(snapshot.OriginalError, kFailure);
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
    ASSERT_TRUE(host->Register(kModule, report));
    ASSERT_TRUE(host->Activate(kModule, report));
    const auto first = host->CreateScope(Scope::kContext, host->HostScope(), report);
    const auto second = host->CreateScope(Scope::kContext, host->HostScope(), report);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    auto borrow = state->Exports.front().Access.TryBorrow();
    state->Finished = false;
    state->BusyValue = 1;
    ASSERT_TRUE(borrow);
    ASSERT_TRUE(host->BeginCloseScope(*first, report));
    const auto busy = host->TryFinishCloseScope(*first, report);
    ASSERT_FALSE(busy);
    ASSERT_EQ(busy.error(), MakeModuleError(ModuleError::kBusy));
    ASSERT_EQ(state->Destroyed, 0);
    ASSERT_FALSE(state->Exports.front().Access.TryBorrow());
    ASSERT_TRUE(host->BeginCloseScope(*second, report));
    ASSERT_TRUE(host->TryFinishCloseScope(*second, report));
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
    ASSERT_TRUE(host->Register(kModule, report));
    const auto first = host->CreateScope(Scope::kContext, host->HostScope(), report);
    const auto second = host->CreateScope(Scope::kContext, host->HostScope(), report);
    state->FailStartAt = 2;
    state->Finished = false;
    const auto activated = host->Activate(kModule, report);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_FALSE(activated);
    ASSERT_EQ(activated.error(), kFailure);
    ASSERT_EQ(report.OriginalError, kFailure);
    ASSERT_EQ(host->Modules().front().State, StaticModuleState::kCleanupPending);
    ASSERT_EQ(state->Destroyed, 0);
    ASSERT_FALSE(host->Activate(kModule, report));
    ASSERT_FALSE(host->TryFinishUnload(kModule, report));
    ASSERT_EQ(report.OriginalError, kFailure);
    state->Finished = true;
    ASSERT_TRUE(host->TryFinishUnload(kModule, report));
    ASSERT_EQ(report.OriginalError, kFailure);
    ASSERT_EQ(state->Destroyed, 2);
    ASSERT_TRUE(host->Load(kModule, report));
    ASSERT_TRUE(host->Register(kModule, report));
    state->FailStartAt = 0;
    ASSERT_TRUE(host->Activate(kModule, report));
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
    auto host = StaticModuleHost::Create({kEntries}, declarations, {}, report);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(kProviderModule, report));
    ASSERT_TRUE(host->Register(kModule, report));
    const auto early = host->Activate(kModule, report);
    ASSERT_FALSE(early);
    ASSERT_EQ(early.error(), MakeModuleError(ModuleError::kDependencyNotActive));
    ASSERT_EQ(state->Made, 0);
    ASSERT_TRUE(host->Activate(kProviderModule, report));
    ASSERT_TRUE(host->Activate(kModule, report));
    const auto context = host->CreateScope(Scope::kContext, host->HostScope(), report);
    ASSERT_TRUE(context);
    ASSERT_TRUE(state->Exports.front().Access.TryBorrow());
    const auto providerUnload = host->BeginUnload(kProviderModule, report);
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
    auto host = StaticModuleHost::Create({kEntries}, declarations, {}, report);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(kProviderModule, report));
    ASSERT_TRUE(host->Register(kModule, report));
    ASSERT_TRUE(host->Activate(kProviderModule, report));
    ASSERT_TRUE(host->Activate(kModule, report));
    const auto scope = host->CreateScope(Scope::kContext, host->HostScope(), report);
    ASSERT_FALSE(scope);
    ASSERT_EQ(scope.error(), MakeModuleError(ModuleError::kTypeMismatch));
    ASSERT_EQ(state->Made, 1);
    ASSERT_EQ(host->Modules().front().State, StaticModuleState::kActive);
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
    ASSERT_TRUE(original->Register(kModule, report));
    ASSERT_TRUE(original->Activate(kModule, report));
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
    ASSERT_TRUE(host->Register(kModule, report));
    ASSERT_TRUE(host->Activate(kModule, report));
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
    ASSERT_TRUE(host->Register(kModule, report));
    ASSERT_TRUE(host->Activate(kModule, report));
    const auto first = host->CreateScope(Scope::kContext, host->HostScope(), report);
    ASSERT_TRUE(first);
    state->FailStartAt = 2;
    state->Finished = false;
    state->BusyValue = 2;
    const auto failed = host->CreateScope(Scope::kContext, host->HostScope(), report);
    const auto error = report.OriginalError;
    const auto busy = host->PollCleanup(report);
    state->Finished = true;
    ASSERT_FALSE(failed);
    ASSERT_EQ(failed.error(), kFailure);
    ASSERT_EQ(error, kFailure);
    ASSERT_FALSE(busy);
    ASSERT_EQ(report.OriginalError, kFailure);
    ASSERT_TRUE(state->Exports.front().Access.TryBorrow());
    ASSERT_TRUE(host->PollCleanup(report));
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
        auto host = StaticModuleHost::Create({kEntries}, declarations, {}, report);
        ASSERT_TRUE(host);
        ASSERT_TRUE(host->Register(kProviderModule, report));
        ASSERT_TRUE(host->Register(kModule, report));
        const auto cycle = host->Activate(kProviderModule, report);
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
    auto host = StaticModuleHost::Create({std::span(kEntries).first<1>()}, declarations, {}, report);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(kModule, report));
    const auto cycle = host->Activate(kModule, report);
    ASSERT_FALSE(cycle);
    ASSERT_EQ(cycle.error(), MakeModuleError(ModuleError::kDependencyCycle));
    ASSERT_EQ(state->Made, 0);
    ASSERT_EQ(host->Modules().front().State, StaticModuleState::kRegistered);
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
    auto host = StaticModuleHost::Create({selected}, declarations, {}, report);
    ASSERT_TRUE(host);
    for (const auto id : host->RegistrationOrder())
    {
        ASSERT_TRUE(host->Register(id, report));
        ASSERT_TRUE(host->Activate(id, report));
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
    ASSERT_TRUE(host->Register(kModule, report));
    ASSERT_TRUE(host->Activate(kModule, report));
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
        const auto result = State->Host->Register(kProviderModule, *State->SharedReport);
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
    auto host = StaticModuleHost::Create({std::span(kEntries).first<1>()}, declarations, {}, report);
    const auto second = Definition(kProviderModule, metadata);
    Fixture::gFirst = &second;
    auto other = StaticModuleHost::Create({std::span(kEntries).first<1>()}, declarations, {}, report);
    ASSERT_TRUE(host);
    ASSERT_TRUE(other);
    state->Host = &*other;
    state->SharedReport = &report;
    ASSERT_TRUE(host->Register(kModule, report));
    ASSERT_TRUE(host->BeginUnload(kModule, report));
    ASSERT_TRUE(host->TryFinishUnload(kModule, report));
    ASSERT_TRUE(state->SawBusy);
    ASSERT_EQ(other->Modules().front().State, StaticModuleState::kLoaded);
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
        auto host = StaticModuleHost::Create({std::span(kEntries).first<1>()}, declarations, {}, report);
        const auto metadata = [](RegistrationContext& context) noexcept -> pg::Status
        { return context.Declare(kProvider, "nested metadata"); };
        const auto second = Definition(kProviderModule, metadata);
        Fixture::gFirst = &second;
        auto other = StaticModuleHost::Create({std::span(kEntries).first<1>()}, declarations, {}, report);
        ASSERT_TRUE(host);
        ASSERT_TRUE(other);
        state->Host = &*other;
        state->SharedReport = &report;
        const auto failed = host->Register(kModule, report);
        ASSERT_FALSE(failed);
        ASSERT_EQ(failed.error(), expected);
        ASSERT_TRUE(state->SawBusy);
        ASSERT_EQ(report.Module, kModule);
        ASSERT_EQ(report.RelatedModule, ModuleId());
        ASSERT_EQ(report.Progress, ModuleOperationProgress::kCleanupPending);
        ASSERT_EQ(report.PrimaryError, expected);
        ASSERT_EQ(report.OriginalError, expected);
        ASSERT_FALSE(report.CleanupError);
        ASSERT_EQ(host->Modules().front().State, StaticModuleState::kCleanupPending);
        ASSERT_EQ(other->Modules().front().State, StaticModuleState::kLoaded);
        ASSERT_TRUE(declarations.Declarations().empty());
        ASSERT_TRUE(other->Register(kProviderModule, report));
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
        ASSERT_TRUE(host->Register(kModule, report));
        ASSERT_TRUE(host->Activate(kModule, report));
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
    ASSERT_TRUE(host->Register(kModule, report));
    ASSERT_TRUE(host->CreateScope(Scope::kContext, host->HostScope(), report));
    state->FailStartAt = 1;
    state->PollError = kCleanupFailure;
    const auto failed = host->Activate(kModule, report);
    const auto snapshot = report;
    state->PollError.reset();
    state->Finished = true;
    ASSERT_FALSE(failed);
    ASSERT_EQ(failed.error(), kFailure);
    ASSERT_EQ(snapshot.PrimaryError, kFailure);
    ASSERT_EQ(snapshot.OriginalError, kFailure);
    ASSERT_EQ(snapshot.CleanupError, kCleanupFailure);
    ASSERT_TRUE(host->TryFinishUnload(kModule, report));
}

TEST(ScopeLifecycle, ShutdownCauseSurvivesRetiringFailedScopeAcrossPolls)
{
    const Fixture callbacks;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(kModule, report));
    ASSERT_TRUE(host->Activate(kModule, report));
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
    ASSERT_TRUE(host->Register(kModule, report));
    ASSERT_TRUE(host->CreateScope(Scope::kContext, host->HostScope(), report));
    state->FailStartAt = 1;
    const auto failed = host->Activate(kModule, report);
    ASSERT_FALSE(failed);
    ASSERT_TRUE(host->TryFinishUnload(kModule, report));
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
    auto host = StaticModuleHost::Create({entries}, declarations, {}, report);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(kThirdModule, report));
    ASSERT_TRUE(host->Activate(kThirdModule, report));
    ASSERT_TRUE(host->Register(kProviderModule, report));
    ASSERT_TRUE(host->Register(kModule, report));
    const auto rejected = host->Activate(kProviderModule, report);
    ASSERT_FALSE(rejected);
    ASSERT_EQ(rejected.error(), MakeModuleError(ModuleError::kDependencyCycle));
    const auto scope = host->CreateScope(Scope::kContext, host->HostScope(), report);
    ASSERT_TRUE(scope);
    ASSERT_EQ(state->Made, 1);
    ASSERT_EQ(host->Modules().back().State, StaticModuleState::kActive);
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
        auto host = StaticModuleHost::Create({std::span(kEntries).first<1>()}, declarations, {}, report);
        ASSERT_TRUE(host);
        ASSERT_TRUE(host->Register(kModule, report));
        const auto failed = host->Activate(kModule, report);
        ASSERT_FALSE(failed);
        ASSERT_EQ(failed.error(), MakeModuleError(ModuleError::kInvalidDeclaration));
        ASSERT_EQ(state->Started, 0);
        ASSERT_EQ(state->Destroyed, state->Made);
        ASSERT_FALSE((!state->Exports.empty() && state->Exports.front().Access.TryBorrow()));
        ASSERT_TRUE(host->TryFinishUnload(kModule, report));
        ASSERT_TRUE(declarations.Declarations().empty());
    }
}

} // namespace
