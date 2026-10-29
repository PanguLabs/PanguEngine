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
#include <cstdlib>
#include <exception>
#include <expected>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace pg::module;
using pg::service::Scope;
constexpr ModuleId kModule{.Bytes = {std::byte{1}}};
constexpr ModuleId kProviderModule{.Bytes = {std::byte{2}}};
constexpr DeclarationId kObject{.Bytes = {std::byte{1}}};
constexpr DeclarationId kProvider{.Bytes = {std::byte{2}}};
constexpr pg::Error kFailure{pg::ErrorDomainId::Constant<0x73636f7065746573ULL>(), 71};
constexpr pg::Error kCleanupFailure{pg::ErrorDomainId::Constant<0x73636f7065746573ULL>(), 72};

struct Export
{
    ScopeHandle Scope;
    ScopeAccess<struct Object> Access;
};
struct Counters
{
    int Registrations{};
    int Made{};
    int Destroyed{};
    int Started{};
    int Stopped{};
    int FailFactoryAt{};
    int FailStartAt{};
    bool Finished{true};
    int BusyValue{};
    std::optional<pg::Error> PollError;
    bool ProviderAlive{};
    bool BrokenOrder{};
    bool WrongDependencyType{};
    bool Reenter{};
    bool SawBusy{};
    StaticModuleHost* Host{};
    ModuleOperationReport* SharedReport{};
    std::string DestroyMarker;
    std::vector<Export> Exports;
    std::vector<int> DestroyOrder;
};
struct Object
{
    std::shared_ptr<Counters> State;
    int Value{};
    bool Provider{};
    bool UsesProvider{};
    Object(std::shared_ptr<Counters> state, int value, bool provider, bool usesProvider)
        : State(std::move(state))
        , Value(value)
        , Provider(provider)
        , UsesProvider(usesProvider)
    {
    }
    ~Object()
    {
        if (UsesProvider && !State->ProviderAlive)
        {
            State->BrokenOrder = true;
        }
        if (Provider)
        {
            State->ProviderAlive = false;
        }
        ++State->Destroyed;
        State->DestroyOrder.push_back(Value);
        if (!State->DestroyMarker.empty())
        {
            std::ofstream(State->DestroyMarker) << "destroyed";
        }
    }
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&) = delete;
    Object& operator=(Object&&) = delete;
};
// ScopeHooks<Object> requires these mutable-reference callback signatures.
// NOLINTNEXTLINE(misc-const-correctness)
[[nodiscard]] pg::Status Start(Object& object) noexcept
{
    ++object.State->Started;
    if (object.Provider)
    {
        object.State->ProviderAlive = true;
    }
    return object.State->Started == object.State->FailStartAt ? pg::Status{std::unexpected(kFailure)} : pg::Status{};
}
// NOLINTNEXTLINE(misc-const-correctness)
void Stop(Object& object) noexcept { ++object.State->Stopped; }
// NOLINTNEXTLINE(misc-const-correctness)
[[nodiscard]] pg::Status Poll(Object& object) noexcept
{
    if (object.State->PollError)
    {
        return std::unexpected(*object.State->PollError);
    }
    return (object.State->Finished || (object.State->BusyValue != 0 && object.Value != object.State->BusyValue))
               ? pg::Status{}
               : pg::Status{std::unexpected(MakeModuleError(ModuleError::kBusy))};
}

struct Fixture
{
    inline static std::shared_ptr<Counters> gState;
    inline static const ModuleDefinition* gFirst{};
    inline static const ModuleDefinition* gSecond{};
    inline static const ModuleDefinition* gThird{};
    static const ModuleDefinition* QueryFirst() noexcept { return gFirst; }
    static const ModuleDefinition* QuerySecond() noexcept { return gSecond; }
    static const ModuleDefinition* QueryThird() noexcept { return gThird; }
    [[nodiscard]] static pg::Status Register(RegistrationContext& context) noexcept
    {
        ++gState->Registrations;
        return context.DeclareFactory(
            kObject, Scope::kContext,
            ScopeFactory(
                [state = gState](ScopeBuildContext& build) noexcept -> pg::Status
                {
                    const int value = ++state->Made;
                    auto object = std::make_unique<Object>(state, value, false, false);
                    auto access =
                        build.Own(std::move(object),
                                  ScopeHooks<Object>{.Start = Start, .RequestStop = Stop, .PollStopped = Poll});
                    if (!access)
                    {
                        return std::unexpected(access.error());
                    }
                    state->Exports.push_back({.Scope = build.Scope(), .Access = *access});
                    if (state->Reenter)
                    {
                        const auto nested =
                            state->Host->CreateScope(Scope::kContext, state->Host->HostScope(), *state->SharedReport);
                        state->SawBusy = !nested && nested.error() == MakeModuleError(ModuleError::kBusy);
                    }
                    return value == state->FailFactoryAt ? pg::Status{std::unexpected(kFailure)} : pg::Status{};
                }),
            {}, "objects");
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

bool ScopeFailureIsLocalAndFactoryRepeats()
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    if (!host || !host->Register(kModule, report) || !host->Activate(kModule, report))
    {
        return false;
    }
    const auto first = host->CreateScope(Scope::kContext, host->HostScope(), report);
    if (!first || state->Exports.size() != 1)
    {
        return false;
    }
    state->FailFactoryAt = 2;
    const auto failed = host->CreateScope(Scope::kContext, host->HostScope(), report);
    const auto rejectedHandle = report.Scope;
    const auto snapshot = report;
    if (failed || failed.error() != kFailure || state->Destroyed != 1 || state->Registrations != 1 ||
        host->Modules().front().State != StaticModuleState::kActive || !state->Exports.front().Access.TryBorrow())
    {
        return false;
    }
    const auto third = host->CreateScope(Scope::kContext, host->HostScope(), report);
    if (!third || third->Slot != rejectedHandle.Slot || third->Generation == rejectedHandle.Generation ||
        host->BeginCloseScope(rejectedHandle, report) || state->Exports.at(1).Access.TryBorrow() ||
        !state->Exports.back().Access.TryBorrow() || snapshot.OriginalError != kFailure)
    {
        return false;
    }
    return host->BeginShutdown(report) && host->TryFinishShutdown(report) && state->Destroyed == 3 &&
           declarations.Declarations().empty();
}

bool BorrowAndWorkDelayDestructionIndependently()
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    if (!host || !host->Register(kModule, report) || !host->Activate(kModule, report))
    {
        return false;
    }
    const auto first = host->CreateScope(Scope::kContext, host->HostScope(), report);
    const auto second = host->CreateScope(Scope::kContext, host->HostScope(), report);
    if (!first || !second)
    {
        return false;
    }
    auto borrow = state->Exports.front().Access.TryBorrow();
    state->Finished = false;
    state->BusyValue = 1;
    if (!borrow || !host->BeginCloseScope(*first, report))
    {
        return false;
    }
    const auto busy = host->TryFinishCloseScope(*first, report);
    if (busy || busy.error() != MakeModuleError(ModuleError::kBusy) || state->Destroyed != 0 ||
        state->Exports.front().Access.TryBorrow())
    {
        return false;
    }
    if (!host->BeginCloseScope(*second, report) || !host->TryFinishCloseScope(*second, report) || state->Destroyed != 1)
    {
        return false;
    }
    state->Finished = false;
    borrow->Release();
    if (host->TryFinishCloseScope(*first, report) || state->Destroyed != 1)
    {
        return false;
    }
    state->Finished = true;
    return host->TryFinishCloseScope(*first, report) && state->Destroyed == 2 && state->Stopped == 2;
}

bool PartialStartFailureStaysCleanupOnlyAndReloadUsesNewTokens()
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    if (!host || !host->Register(kModule, report))
    {
        return false;
    }
    const auto first = host->CreateScope(Scope::kContext, host->HostScope(), report);
    const auto second = host->CreateScope(Scope::kContext, host->HostScope(), report);
    state->FailStartAt = 2;
    state->Finished = false;
    const auto activated = host->Activate(kModule, report);
    if (!first || !second || activated || activated.error() != kFailure || report.OriginalError != kFailure ||
        host->Modules().front().State != StaticModuleState::kCleanupPending || state->Destroyed != 0 ||
        host->Activate(kModule, report) || host->TryFinishUnload(kModule, report) || report.OriginalError != kFailure)
    {
        return false;
    }
    state->Finished = true;
    if (!host->TryFinishUnload(kModule, report) || report.OriginalError != kFailure || state->Destroyed != 2 ||
        !host->Load(kModule, report) || !host->Register(kModule, report))
    {
        return false;
    }
    state->FailStartAt = 0;
    if (!host->Activate(kModule, report) || state->Registrations != 2 || state->Exports.size() != 4 ||
        state->Exports.front().Access.TryBorrow() || !state->Exports.back().Access.TryBorrow())
    {
        return false;
    }
    return host->BeginShutdown(report) && host->TryFinishShutdown(report) && state->Destroyed == 4;
}

bool DependenciesPreserveParentAndReverseDestruction()
{
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
    if (!host || !host->Register(kProviderModule, report) || !host->Register(kModule, report))
    {
        return false;
    }
    const auto early = host->Activate(kModule, report);
    if (early || early.error() != MakeModuleError(ModuleError::kDependencyNotActive) || state->Made != 0 ||
        !host->Activate(kProviderModule, report) || !host->Activate(kModule, report))
    {
        return false;
    }
    const auto context = host->CreateScope(Scope::kContext, host->HostScope(), report);
    if (!context || !state->Exports.front().Access.TryBorrow())
    {
        return false;
    }
    const auto providerUnload = host->BeginUnload(kProviderModule, report);
    if (providerUnload || providerUnload.error() != MakeModuleError(ModuleError::kDependencyInUse))
    {
        return false;
    }
    if (!host->BeginShutdown(report) || !host->TryFinishShutdown(report))
    {
        return false;
    }
    return !state->BrokenOrder && state->DestroyOrder == std::vector<int>{11, 1};
}

bool WrongDependencyTypeCannotCommit()
{
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
    if (!host || !host->Register(kProviderModule, report) || !host->Register(kModule, report) ||
        !host->Activate(kProviderModule, report) || !host->Activate(kModule, report))
    {
        return false;
    }
    const auto scope = host->CreateScope(Scope::kContext, host->HostScope(), report);
    return !scope && scope.error() == MakeModuleError(ModuleError::kTypeMismatch) && state->Made == 1 &&
           host->Modules().front().State == StaticModuleState::kActive && host->PollCleanup(report);
}

bool HandlesAndMovingHostPreserveOwnership()
{
    RegistrationScope declarations;
    RegistrationScope otherDeclarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto original = Host(declarations, report, state);
    auto foreign = Host(otherDeclarations, report, state);
    if (!original || !foreign || !original->Register(kModule, report) || !original->Activate(kModule, report))
    {
        return false;
    }
    auto moved = std::move(*original);
    if (moved.CreateScope(Scope::kContext, foreign->HostScope(), report) ||
        moved.CreateScope(Scope::kWorld, moved.HostScope(), report))
    {
        return false;
    }
    const auto context = moved.CreateScope(Scope::kContext, moved.HostScope(), report);
    if (!context || !state->Exports.front().Access.TryBorrow())
    {
        return false;
    }
    const auto world = moved.CreateScope(Scope::kWorld, *context, report);
    return world && moved.BeginCloseScope(*context, report) && moved.TryFinishCloseScope(*context, report) &&
           !moved.BeginCloseScope(*world, report) && moved.BeginShutdown(report) && moved.TryFinishShutdown(report);
}

bool ManagementReentryAndAliasedReportDoNotCorruptOuterOperation()
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    if (!host || !host->Register(kModule, report) || !host->Activate(kModule, report))
    {
        return false;
    }
    state->Reenter = true;
    state->Host = &*host;
    state->SharedReport = &report;
    const auto scope = host->CreateScope(Scope::kContext, host->HostScope(), report);
    return scope && state->SawBusy && !report.PrimaryError && report.Scope == *scope &&
           report.Progress == ModuleOperationProgress::kScopeReady;
}

bool FailedScopeWithoutHandleRemainsTrackedUntilWorkStops()
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    if (!host || !host->Register(kModule, report) || !host->Activate(kModule, report))
    {
        return false;
    }
    const auto first = host->CreateScope(Scope::kContext, host->HostScope(), report);
    if (!first)
    {
        return false;
    }
    state->FailStartAt = 2;
    state->Finished = false;
    state->BusyValue = 2;
    const auto failed = host->CreateScope(Scope::kContext, host->HostScope(), report);
    const auto error = report.OriginalError;
    const auto busy = host->PollCleanup(report);
    state->Finished = true;
    if (failed || failed.error() != kFailure || error != kFailure || busy || report.OriginalError != kFailure ||
        !state->Exports.front().Access.TryBorrow() || !host->PollCleanup(report) || state->Destroyed != 1)
    {
        return false;
    }
    return host->BeginShutdown(report) && host->TryFinishShutdown(report) && state->Destroyed == 2;
}

bool RejectsCombinedAndLocalCyclesBeforeFactories()
{
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
        if (!host || !host->Register(kProviderModule, report) || !host->Register(kModule, report))
        {
            return false;
        }
        const auto cycle = host->Activate(kProviderModule, report);
        if (cycle || cycle.error() != MakeModuleError(ModuleError::kDependencyCycle) || state->Made != 0)
        {
            return false;
        }
    }
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto registerLocal = [](RegistrationContext& context) noexcept -> pg::Status
    {
        for (const auto id : {kObject, kProvider})
        {
            const std::array dependencies{
                ScopeDependency{.Id = id == kObject ? kProvider : kObject, .Scope = Scope::kContext},
            };
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
    if (!host || !host->Register(kModule, report))
    {
        return false;
    }
    const auto cycle = host->Activate(kModule, report);
    return !cycle && cycle.error() == MakeModuleError(ModuleError::kDependencyCycle) && state->Made == 0 &&
           host->Modules().front().State == StaticModuleState::kRegistered;
}

bool ScopeBudgetIsCheckedBeforeCallbacksAndSlotsCanBeReused()
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    if (!host)
    {
        return false;
    }
    std::vector<ScopeHandle> contexts;
    for (int index = 0; index < 255; ++index)
    {
        const auto scope = host->CreateScope(Scope::kContext, host->HostScope(), report);
        if (!scope)
        {
            return false;
        }
        contexts.push_back(*scope);
    }
    const auto overflow = host->CreateScope(Scope::kContext, host->HostScope(), report);
    if (overflow || overflow.error() != MakeModuleError(ModuleError::kLimitExceeded) || state->Made != 0 ||
        !host->BeginCloseScope(contexts.front(), report) || !host->TryFinishCloseScope(contexts.front(), report))
    {
        return false;
    }
    const auto reused = host->CreateScope(Scope::kContext, host->HostScope(), report);
    return reused && reused->Slot == contexts.front().Slot && reused->Generation != contexts.front().Generation &&
           !host->BeginCloseScope(contexts.front(), report);
}

bool ModuleDependencyKeepsProviderWorkAliveDuringBusyCleanup()
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    Fixture::gState = state;
    constexpr std::array kDependencies{ModuleDependency{.Id = kProviderModule}};
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
    const auto first = Definition(kModule, registration, kDependencies);
    const auto second = Definition(kProviderModule, Fixture::RegisterProvider);
    Fixture::gFirst = &first;
    Fixture::gSecond = &second;
    auto host = StaticModuleHost::Create({kEntries}, declarations, {}, report);
    if (!host || !host->Register(kProviderModule, report) || !host->Register(kModule, report) ||
        !host->Activate(kProviderModule, report) || !host->Activate(kModule, report))
    {
        return false;
    }
    state->Finished = false;
    state->BusyValue = 2;
    if (!host->BeginShutdown(report))
    {
        state->Finished = true;
        return false;
    }
    const auto busy = host->TryFinishShutdown(report);
    const bool retained = !busy && busy.error() == MakeModuleError(ModuleError::kBusy) && state->Destroyed == 0 &&
                          state->Stopped == 1 && state->ProviderAlive;
    state->Finished = true;
    const auto finished = host->TryFinishShutdown(report);
    return retained && finished && !state->BrokenOrder && state->DestroyOrder == std::vector<int>{2, 1};
}

bool ShutdownRetainsUnreturnedScopeFailure()
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    if (!host || !host->Register(kModule, report) || !host->Activate(kModule, report))
    {
        return false;
    }
    state->FailStartAt = 1;
    state->Finished = false;
    const auto failed = host->CreateScope(Scope::kContext, host->HostScope(), report);
    const auto original = report.OriginalError;
    if (!host->BeginShutdown(report))
    {
        state->Finished = true;
        return false;
    }
    const auto busy = host->TryFinishShutdown(report);
    const bool retained = !failed && original == kFailure && !busy && report.OriginalError == kFailure &&
                          report.PrimaryError == kFailure && report.CleanupError == MakeModuleError(ModuleError::kBusy);
    state->Finished = true;
    const auto done = host->TryFinishShutdown(report);
    return retained && done && report.OriginalError == kFailure && state->Destroyed == 1;
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

bool FactoryRetirementBlocksSharedDeclarationReentry()
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    Fixture::gState = state;
    const auto callback = [](RegistrationContext& context) noexcept -> pg::Status
    {
        return context.DeclareFactory(kObject, Scope::kContext,
                                      ScopeFactory(
                                          [guard = std::make_unique<RetireCallback>(Fixture::gState)](
                                              ScopeBuildContext& build) noexcept -> pg::Status
                                          {
                                              [[maybe_unused]] const auto* retained = guard.get();
                                              const auto object = build.Own(std::make_unique<int>(1));
                                              return object ? pg::Status{} : std::unexpected(object.error());
                                          }));
    };
    const auto metadata = [](RegistrationContext& context) noexcept -> pg::Status
    { return context.Declare(kProvider, "nested metadata"); };
    const auto first = Definition(kModule, callback);
    Fixture::gFirst = &first;
    auto host = StaticModuleHost::Create({std::span(kEntries).first<1>()}, declarations, {}, report);
    const auto second = Definition(kProviderModule, metadata);
    Fixture::gFirst = &second;
    auto other = StaticModuleHost::Create({std::span(kEntries).first<1>()}, declarations, {}, report);
    if (!host || !other)
    {
        return false;
    }
    state->Host = &*other;
    state->SharedReport = &report;
    if (!host->Register(kModule, report) || !host->BeginUnload(kModule, report) ||
        !host->TryFinishUnload(kModule, report))
    {
        return false;
    }
    return state->SawBusy && other->Modules().front().State == StaticModuleState::kLoaded &&
           declarations.Declarations().empty() && report.Module == kModule;
}

bool ReportSeparatesOriginalFailureFromCleanupResult()
{
    for (int mode = 0; mode < 3; ++mode)
    {
        RegistrationScope declarations;
        ModuleOperationReport report;
        const auto state = std::make_shared<Counters>();
        auto host = Host(declarations, report, state);
        if (!host || !host->Register(kModule, report) || !host->Activate(kModule, report))
        {
            return false;
        }
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
        if (!host->PollCleanup(report))
        {
            return false;
        }
        const std::optional<pg::Error> expected =
            mode == 0 ? std::nullopt : std::optional(mode == 1 ? MakeModuleError(ModuleError::kBusy) : kCleanupFailure);
        if (failed || failed.error() != kFailure || snapshot.PrimaryError != kFailure ||
            snapshot.OriginalError != kFailure || snapshot.CleanupError != expected)
        {
            return false;
        }
    }
    return true;
}

bool ActivationReportSeparatesCleanupError()
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    if (!host || !host->Register(kModule, report) || !host->CreateScope(Scope::kContext, host->HostScope(), report))
    {
        return false;
    }
    state->FailStartAt = 1;
    state->PollError = kCleanupFailure;
    const auto failed = host->Activate(kModule, report);
    const auto snapshot = report;
    state->PollError.reset();
    state->Finished = true;
    return !failed && failed.error() == kFailure && snapshot.PrimaryError == kFailure &&
           snapshot.OriginalError == kFailure && snapshot.CleanupError == kCleanupFailure &&
           host->TryFinishUnload(kModule, report);
}

bool ShutdownCauseSurvivesRetiringFailedScopeAcrossPolls()
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    if (!host || !host->Register(kModule, report) || !host->Activate(kModule, report) ||
        !host->CreateScope(Scope::kContext, host->HostScope(), report))
    {
        return false;
    }
    auto held = state->Exports.front().Access.TryBorrow();
    if (!held)
    {
        return false;
    }
    state->FailStartAt = 2;
    state->Finished = false;
    state->BusyValue = 2;
    const auto failed = host->CreateScope(Scope::kContext, host->HostScope(), report);
    state->Finished = true;
    if (failed || !host->BeginShutdown(report))
    {
        return false;
    }
    const auto first = host->TryFinishShutdown(report);
    const auto firstReport = report;
    const auto second = host->TryFinishShutdown(report);
    const auto secondReport = report;
    held->Release();
    const auto final = host->TryFinishShutdown(report);
    return !first && !second && final && firstReport.OriginalError == kFailure &&
           secondReport.OriginalError == kFailure && report.OriginalError == kFailure && state->Destroyed == 2;
}

bool ShutdownDoesNotImportCompletedHistoricalFailure()
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto host = Host(declarations, report, state);
    if (!host || !host->Register(kModule, report) || !host->CreateScope(Scope::kContext, host->HostScope(), report))
    {
        return false;
    }
    state->FailStartAt = 1;
    const auto failed = host->Activate(kModule, report);
    if (failed || !host->TryFinishUnload(kModule, report) || !host->BeginShutdown(report) ||
        !host->TryFinishShutdown(report))
    {
        return false;
    }
    return !report.OriginalError && !report.PrimaryError;
}

bool InactiveDeclarationCycleDoesNotBlockUnrelatedActiveScope()
{
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
    if (!host || !host->Register(kThirdModule, report) || !host->Activate(kThirdModule, report) ||
        !host->Register(kProviderModule, report) || !host->Register(kModule, report))
    {
        return false;
    }
    const auto rejected = host->Activate(kProviderModule, report);
    if (rejected || rejected.error() != MakeModuleError(ModuleError::kDependencyCycle))
    {
        return false;
    }
    const auto scope = host->CreateScope(Scope::kContext, host->HostScope(), report);
    return scope && state->Made == 1 && host->Modules().back().State == StaticModuleState::kActive;
}

bool FactoryMustOwnExactlyOneInactiveObject()
{
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
        if (!host || !host->Register(kModule, report))
        {
            return false;
        }
        const auto failed = host->Activate(kModule, report);
        if (failed || failed.error() != MakeModuleError(ModuleError::kInvalidDeclaration) || state->Started != 0 ||
            state->Destroyed != state->Made || (!state->Exports.empty() && state->Exports.front().Access.TryBorrow()) ||
            !host->TryFinishUnload(kModule, report) || !declarations.Declarations().empty())
        {
            return false;
        }
    }
    return true;
}

int DestroyBusy(const std::string& marker)
{
    std::set_terminate([] noexcept { std::_Exit(91); });
    RegistrationScope declarations;
    ModuleOperationReport report;
    const auto state = std::make_shared<Counters>();
    auto result = Host(declarations, report, state);
    if (!result)
    {
        return 2;
    }
    auto host = std::make_unique<StaticModuleHost>(std::move(*result));
    if (!host->Register(kModule, report) || !host->Activate(kModule, report) ||
        !host->CreateScope(Scope::kContext, host->HostScope(), report))
    {
        return 2;
    }
    auto const borrow = state->Exports.front().Access.TryBorrow();
    if (!borrow)
    {
        return 2;
    }
    state->DestroyMarker = marker;
    std::ofstream(marker + ".ready") << "ready";
    host.reset();
    return 0;
}
} // namespace

int main(int argc, char* const* argv)
{
    const std::span args(argv, static_cast<std::size_t>(argc));
    if (args.size() == 3 && std::string_view(args.subspan(1).front()) == "--destroy-busy")
    {
        return DestroyBusy(args.back());
    }
    const std::array tests{
        std::pair{"factory owns exactly one inactive object", FactoryMustOwnExactlyOneInactiveObject},
        std::pair{"reports separate original and cleanup errors", ReportSeparatesOriginalFailureFromCleanupResult},
        std::pair{"activation report separates cleanup error", ActivationReportSeparatesCleanupError},
        std::pair{"shutdown cause survives Scope retirement across polls",
                  ShutdownCauseSurvivesRetiringFailedScopeAcrossPolls},
        std::pair{"shutdown excludes completed historical failure", ShutdownDoesNotImportCompletedHistoricalFailure},
        std::pair{"inactive declaration cycle does not block Active Scope",
                  InactiveDeclarationCycleDoesNotBlockUnrelatedActiveScope},
        std::pair{"module dependency retains Provider while cleanup is Busy",
                  ModuleDependencyKeepsProviderWorkAliveDuringBusyCleanup},
        std::pair{"shutdown retains unreturned Scope failure", ShutdownRetainsUnreturnedScopeFailure},
        std::pair{"factory retirement blocks shared declaration reentry",
                  FactoryRetirementBlocksSharedDeclarationReentry},
        std::pair{"failed Scope without handle remains tracked", FailedScopeWithoutHandleRemainsTrackedUntilWorkStops},
        std::pair{"rejects combined and local cycles before factories", RejectsCombinedAndLocalCyclesBeforeFactories},
        std::pair{"scope budget checked and slots reused", ScopeBudgetIsCheckedBeforeCallbacksAndSlotsCanBeReused},
        std::pair{"scope failure is local and factory repeats", ScopeFailureIsLocalAndFactoryRepeats},
        std::pair{"borrow and work delay destruction independently", BorrowAndWorkDelayDestructionIndependently},
        std::pair{"partial start failure stays cleanup-only and reload uses new tokens",
                  PartialStartFailureStaysCleanupOnlyAndReloadUsesNewTokens},
        std::pair{"dependencies preserve parent and reverse destruction",
                  DependenciesPreserveParentAndReverseDestruction},
        std::pair{"wrong dependency type cannot commit", WrongDependencyTypeCannotCommit},
        std::pair{"handles and moving Host preserve ownership", HandlesAndMovingHostPreserveOwnership},
        std::pair{"management reentry and aliased report preserve outer operation",
                  ManagementReentryAndAliasedReportDoNotCorruptOuterOperation},
    };
    int failures = 0;
    for (const auto& [name, test] : tests)
    {
        if (!test())
        {
            std::cerr << "FAIL: " << name << '\n';
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}
