#include "EngineHostTestFixture.h"
#include <EngineHost.h>
#include <EngineHostTestAccess.h>
#include <FailureContext.h>
#include <HostOperation.h>
#include <HostServiceAccess.h>
#include <HostStartupConfig.h>
#include <ModuleDeclaration.h>
#include <ModuleDescriptor.h>
#include <ModuleDiagnostics.h>
#include <ModuleError.h>
#include <ModuleIdentity.h>
#include <ModuleManager.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <gtest/gtest.h>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#ifdef PG_TEST_MODULAR
#include <ScopeTestSupport.h>
#include <SharedFixtureContract.h>
#include <SharedFixturePaths.h>
#endif

namespace pg::engine::test
{
namespace
{
constexpr module::ModuleId kDrainContextModule{.Bytes = {std::byte{0xe2}}};
constexpr module::ModuleId kDrainWorldModule{.Bytes = {std::byte{0xe1}}};
constexpr module::DeclarationId kDrainContextResource{.Bytes = {std::byte{0xe2}}};
constexpr module::DeclarationId kDrainWorldResource{.Bytes = {std::byte{0xe1}}};

enum class Event : std::uint8_t
{
    kParentStop,
    kWorldStop,
    kWorldPoll,
    kWorldDestroy,
};

struct Observation
{
    std::vector<Event> Events;
    bool FailContext{};
    bool FailWorldStart{};
    bool FailWorldPoll{};
    std::string_view WorldStartFailure{"World 资源启动失败"};
    module::ScopeHandle RecoveredWorld;
};

std::shared_ptr<Observation>& PendingObservation()
{
    static std::shared_ptr<Observation> state;
    return state;
}

struct ContextResource
{
    std::shared_ptr<Observation> State;
    int Value{42};
    bool FailedStart{};
};

struct WorldResource
{
    WorldResource(std::shared_ptr<Observation> state, module::ScopeHandle scope) noexcept
        : State(std::move(state))
        , Scope(scope)
    {
    }
    WorldResource(const WorldResource&) = delete;
    WorldResource& operator=(const WorldResource&) = delete;
    WorldResource(WorldResource&&) = delete;
    WorldResource& operator=(WorldResource&&) = delete;
    ~WorldResource() { State->Events.push_back(Event::kWorldDestroy); }

    std::shared_ptr<Observation> State;
    module::ScopeHandle Scope;
    int Value{42};
};

Status RegisterContext(module::RegistrationContext& context) noexcept
{
    return context.DeclareFactory(
        kDrainContextResource, service::Scope::kContext,
        module::ScopeFactory(
            [state = PendingObservation()](module::ScopeBuildContext& build) noexcept -> Status
            {
                const auto owned = build.Own(
                    std::make_unique<ContextResource>(
                        ContextResource{.State = state, .FailedStart = state->FailContext}),
                    module::ScopeHooks<ContextResource>{
                        .Start = [](ContextResource& resource, FailureContext& failure) noexcept -> Status
                        {
                            if (resource.FailedStart)
                            {
                                failure.SetPrimary(kStartupError, "Context 资源启动失败");
                                return std::unexpected(kStartupError);
                            }
                            return {};
                        },
                        .RequestStop = [](ContextResource& resource) noexcept
                        { resource.State->Events.push_back(Event::kParentStop); },
                        .PollStopped = [](ContextResource& resource, FailureContext& failure) noexcept -> Status
                        {
                            if (resource.FailedStart && resource.State->FailContext)
                            {
                                failure.SetPrimary(kStopError, "Context 资源清理失败");
                                return std::unexpected(kStopError);
                            }
                            return {};
                        },
                    });
                return owned ? Status{} : std::unexpected(owned.error());
            }));
}

Status RegisterWorld(module::RegistrationContext& context) noexcept
{
    return context.DeclareFactory(
        kDrainWorldResource, service::Scope::kWorld,
        module::ScopeFactory(
            [state = PendingObservation()](module::ScopeBuildContext& build) noexcept -> Status
            {
                const auto owned = build.Own(
                    std::make_unique<WorldResource>(state, build.Scope()),
                    module::ScopeHooks<WorldResource>{
                        .Start = [](WorldResource& resource, FailureContext& failure) noexcept -> Status
                        {
                            if (resource.State->FailWorldStart)
                            {
                                failure.SetPrimary(kStartupError, resource.State->WorldStartFailure);
                                return std::unexpected(kStartupError);
                            }
                            return {};
                        },
                        .RequestStop = [](WorldResource& resource) noexcept
                        { resource.State->Events.push_back(Event::kWorldStop); },
                        .PollStopped = [](WorldResource& resource, FailureContext& failure) noexcept -> Status
                        {
                            resource.State->Events.push_back(Event::kWorldPoll);
                            if (resource.State->FailWorldPoll && resource.Scope != resource.State->RecoveredWorld)
                            {
                                failure.SetPrimary(kStopError, "World 资源清理失败");
                                return std::unexpected(kStopError);
                            }
                            return {};
                        },
                    });
                return owned ? Status{} : std::unexpected(owned.error());
            }));
}

const module::ModuleDefinition* QueryDrainContext() noexcept
{
    static constexpr module::ModuleDefinition kDefinition{
        .Descriptor =
            {
                .Id = kDrainContextModule,
                .Dependencies = {},
                .DisplayName = "DrainContext",
                .BuildIdentity = {},
            },
        .RegisterModule = RegisterContext,
    };
    return &kDefinition;
}

const module::ModuleDefinition* QueryDrainWorld() noexcept
{
    static constexpr module::ModuleDefinition kDefinition{
        .Descriptor = {.Id = kDrainWorldModule, .Dependencies = {}, .DisplayName = "DrainWorld", .BuildIdentity = {}},
        .RegisterModule = RegisterWorld,
    };
    return &kDefinition;
}

class EngineHostWorldShutdown : public testing::Test
{
protected:
    void StartHost(std::span<const module::ModuleLoadCandidate> candidates = {})
    {
        PendingObservation() = State;
        // Manager 逆接纳顺序处理模块，旧关闭路径会先处理父 Context 所属模块。
        constexpr std::array kQueries{QueryDrainWorld, QueryDrainContext};
        HostStartupConfig config;
        config.AdditionalResidentQueries = kQueries;
        config.Modules = candidates;
        config.Logging = DisabledLogging();
        ASSERT_TRUE(Host.Start(config, Report));
        const auto context = Host.CreateContext(Report);
        ASSERT_TRUE(context);
        Context = *context;
    }

    void TearDown() override
    {
        State->FailContext = false;
        State->FailWorldPoll = false;
        PendingObservation().reset();
    }

public:
    std::shared_ptr<Observation> State = std::make_shared<Observation>();
    EngineHost Host;
    HostOperationReport Report;
    module::ScopeHandle Context;
};
} // namespace

TEST_F(EngineHostWorldShutdown, ClosingRejectsOldTokens)
{
    ASSERT_NO_FATAL_FAILURE(StartHost());
    const auto world = Host.CreateWorld(Context, Report);
    ASSERT_TRUE(world);
    const auto worldAccess =
        detail::HostResourceAccess::Acquire<WorldResource>(Host, {
                                                                     .Scope = *world,
                                                                     .Owner = kDrainWorldModule,
                                                                     .Declaration = kDrainWorldResource,
                                                                     .Kind = service::Scope::kWorld,
                                                                 });
    const auto contextAccess =
        detail::HostResourceAccess::Acquire<ContextResource>(Host, {
                                                                       .Scope = Context,
                                                                       .Owner = kDrainContextModule,
                                                                       .Declaration = kDrainContextResource,
                                                                       .Kind = service::Scope::kContext,
                                                                   });
    ASSERT_TRUE(worldAccess);
    ASSERT_TRUE(contextAccess);
    auto worldBorrow = worldAccess->TryBorrow();
    auto contextBorrow = contextAccess->TryBorrow();
    ASSERT_TRUE(worldBorrow);
    ASSERT_TRUE(contextBorrow);
    const auto* manager = detail::EngineHostTestAccess::Manager(Host);
    ASSERT_NE(manager, nullptr);
    const auto worldModule = manager->Find(kDrainWorldModule);
    if (!worldModule)
    {
        FAIL() << "World Module 未接纳";
    }

    ASSERT_TRUE(Host.BeginShutdown(Report));
    ASSERT_TRUE(Host.BeginShutdown(Report));
    EXPECT_TRUE(State->Events.empty());
    const auto snapshot = manager->Inspect(*worldModule);
    ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot->State, module::ModuleState::kActive);
    EXPECT_FALSE(worldAccess->TryBorrow());
    EXPECT_FALSE(contextAccess->TryBorrow());
    EXPECT_FALSE(manager->TryPinForBusiness(*worldModule));
    EXPECT_EQ((*worldBorrow)->Value, 42);
    EXPECT_EQ((*contextBorrow)->Value, 42);
    EXPECT_FALSE(Host.CreateWorld(Context, Report));

    worldBorrow->Release();
    contextBorrow->Release();
    ASSERT_TRUE(Host.TryFinishShutdown(Report));
    ASSERT_TRUE(Host.TryFinishShutdown(Report));
    EXPECT_EQ(std::ranges::count(State->Events, Event::kWorldDestroy), 1);
}

TEST_F(EngineHostWorldShutdown, ClosingParentWaitsForWorld)
{
    ASSERT_NO_FATAL_FAILURE(StartHost());
    const auto world = Host.CreateWorld(Context, Report);
    ASSERT_TRUE(world);
    const auto access = detail::HostResourceAccess::Acquire<WorldResource>(Host, {
                                                                                     .Scope = *world,
                                                                                     .Owner = kDrainWorldModule,
                                                                                     .Declaration = kDrainWorldResource,
                                                                                     .Kind = service::Scope::kWorld,
                                                                                 });
    ASSERT_TRUE(access);
    auto borrow = access->TryBorrow();
    ASSERT_TRUE(borrow);
    ASSERT_TRUE(Host.BeginCloseScope(Context, Report));
    ASSERT_TRUE(Host.BeginShutdown(Report));

    const auto pending = Host.TryFinishShutdown(Report);
    ASSERT_FALSE(pending);
    EXPECT_EQ(pending.error(), module::MakeModuleError(module::ModuleError::kBusy));
    EXPECT_EQ(std::ranges::count(State->Events, Event::kParentStop), 0);
    EXPECT_EQ(std::ranges::count(State->Events, Event::kWorldDestroy), 0);
    const auto& moduleReport = Report.ModuleReport;
    if (!moduleReport)
    {
        FAIL() << "缺少 World 排空报告";
    }
    ASSERT_EQ(moduleReport->WaitReasonCount, 1U);
    EXPECT_EQ(moduleReport->WaitReasons.front().Kind, module::ModuleWaitKind::kBorrow);
    EXPECT_EQ(moduleReport->WaitReasons.front().Scope, *world);

    borrow->Release();
    ASSERT_TRUE(Host.TryFinishShutdown(Report));
    EXPECT_EQ(State->Events,
              (std::vector{Event::kWorldStop, Event::kWorldPoll, Event::kWorldDestroy, Event::kParentStop}));
}

TEST_F(EngineHostWorldShutdown, UndeliveredWorldKeepsOriginalError)
{
    ASSERT_NO_FATAL_FAILURE(StartHost());
    State->FailWorldStart = true;
    State->FailWorldPoll = true;
    const auto world = Host.CreateWorld(Context, Report);
    ASSERT_FALSE(world);
    EXPECT_EQ(world.error(), kStartupError);
    const auto& moduleReport = Report.ModuleReport;
    if (!moduleReport)
    {
        FAIL() << "缺少 World 创建失败报告";
    }
    EXPECT_EQ(moduleReport->OriginalError, kStartupError);
    EXPECT_EQ(moduleReport->CleanupError, kStopError);
    const auto failedScope = moduleReport->Scope;
    EXPECT_TRUE(failedScope.IsValid());
    EXPECT_EQ(std::ranges::count(State->Events, Event::kWorldPoll), 1);

    ASSERT_TRUE(Host.BeginShutdown(Report));
    const auto pending = Host.TryFinishShutdown(Report);
    ASSERT_FALSE(pending);
    EXPECT_EQ(pending.error(), kStopError);
    if (!moduleReport)
    {
        FAIL() << "缺少 World 排空报告";
    }
    EXPECT_EQ(moduleReport->PrimaryError, kStartupError);
    EXPECT_EQ(moduleReport->OriginalError, kStartupError);
    EXPECT_EQ(moduleReport->CleanupError, kStopError);
    EXPECT_EQ(moduleReport->Scope, failedScope);
    EXPECT_EQ(moduleReport->Module, kDrainWorldModule);
    EXPECT_EQ(moduleReport->Declaration, kDrainWorldResource);
    EXPECT_EQ(std::ranges::count(State->Events, Event::kParentStop), 0);

    State->FailWorldPoll = false;
    ASSERT_TRUE(Host.TryFinishShutdown(Report));
    EXPECT_EQ(std::ranges::count(State->Events, Event::kWorldDestroy), 1);
    if (!moduleReport)
    {
        FAIL() << "缺少宿主关闭报告";
    }
    EXPECT_EQ(moduleReport->OriginalError, kStartupError);
    EXPECT_EQ(moduleReport->Scope, failedScope);
    EXPECT_EQ(moduleReport->FailureOrigin, module::ModuleFailureOrigin::kStart);
    EXPECT_FALSE(moduleReport->CleanupError);
}

TEST_F(EngineHostWorldShutdown, EqualWorldErrorsKeepFirstFailureAfterRetirement)
{
    ASSERT_NO_FATAL_FAILURE(StartHost());
    State->FailWorldStart = true;
    State->FailWorldPoll = true;
    constexpr std::string_view kFirstFailure = "第一个 World 启动失败";
    State->WorldStartFailure = kFirstFailure;
    const auto first = Host.CreateWorld(Context, Report);
    ASSERT_FALSE(first);
    EXPECT_EQ(first.error(), kStartupError);
    const auto& moduleReport = Report.ModuleReport;
    if (!moduleReport)
    {
        FAIL() << "缺少第一个 World 的创建失败报告";
    }
    const auto firstScope = moduleReport->Scope;
    ASSERT_TRUE(firstScope.IsValid());

    State->WorldStartFailure = "第二个 World 启动失败";
    const auto second = Host.CreateWorld(Context, Report);
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error(), kStartupError);
    if (!moduleReport)
    {
        FAIL() << "缺少第二个 World 的创建失败报告";
    }
    const auto secondScope = moduleReport->Scope;
    ASSERT_TRUE(secondScope.IsValid());
    ASSERT_NE(secondScope, firstScope);

    State->FailContext = true;
    const auto context = Host.CreateContext(Report);
    ASSERT_FALSE(context);
    EXPECT_EQ(context.error(), kStartupError);

    ASSERT_TRUE(Host.BeginShutdown(Report));
    if (!moduleReport)
    {
        FAIL() << "缺少宿主开始关闭的报告";
    }
    EXPECT_EQ(moduleReport->Scope, firstScope);
    State->RecoveredWorld = firstScope;
    const auto pending = Host.TryFinishShutdown(Report);
    ASSERT_FALSE(pending);
    EXPECT_EQ(pending.error(), kStopError);
    EXPECT_EQ(std::ranges::count(State->Events, Event::kWorldDestroy), 1);
    if (!moduleReport)
    {
        FAIL() << "缺少 World 排空报告";
    }
    const auto& report = *moduleReport;
    EXPECT_EQ(report.PrimaryError, kStartupError);
    EXPECT_EQ(report.OriginalError, kStartupError);
    EXPECT_EQ(report.CleanupError, kStopError);
    EXPECT_EQ(report.Scope, firstScope);
    EXPECT_EQ(report.Module, kDrainWorldModule);
    EXPECT_EQ(report.Declaration, kDrainWorldResource);
    EXPECT_EQ(report.FailureOrigin, module::ModuleFailureOrigin::kStart);
    const auto primary = report.Failure.PrimarySite();
    const auto cleanup = report.Failure.SecondarySite(0);
    if (!primary || !cleanup)
    {
        FAIL() << "缺少 World 启动或清理诊断";
    }
    EXPECT_EQ(primary->Message(), kFirstFailure);
    EXPECT_EQ(cleanup->Cause, kStopError);

    State->RecoveredWorld = secondScope;
    const auto modulesPending = Host.TryFinishShutdown(Report);
    ASSERT_FALSE(modulesPending);
    EXPECT_EQ(modulesPending.error(), kStopError);
    EXPECT_EQ(std::ranges::count(State->Events, Event::kWorldDestroy), 2);
    if (!moduleReport)
    {
        FAIL() << "缺少 Module 关闭报告";
    }
    EXPECT_EQ(moduleReport->Scope, firstScope);
    EXPECT_EQ(moduleReport->Module, kDrainWorldModule);
    EXPECT_EQ(moduleReport->Declaration, kDrainWorldResource);
    const auto modulePrimary = moduleReport->Failure.PrimarySite();
    if (!modulePrimary)
    {
        FAIL() << "缺少 Module 关闭报告的首错诊断";
    }
    EXPECT_EQ(modulePrimary->Message(), kFirstFailure);

    State->FailContext = false;
    ASSERT_TRUE(Host.TryFinishShutdown(Report));
    if (!moduleReport)
    {
        FAIL() << "缺少宿主关闭报告";
    }
    EXPECT_EQ(moduleReport->Scope, firstScope);
    EXPECT_FALSE(moduleReport->CleanupError);
}

TEST_F(EngineHostWorldShutdown, WorldDrainDoesNotWaitForUnrelatedPin)
{
#ifdef PG_TEST_MODULAR
    pg::test::Observer().Reset();
    const auto pinnedId = module::fixture::kSharedProviderId;
    const std::array candidates{
        module::ModuleLoadCandidate{
            module::SharedModuleCandidate{.ExpectedId = pinnedId, .Path = module::fixture::kSharedProviderPath},
        },
    };
    ASSERT_NO_FATAL_FAILURE(StartHost(candidates));
#else
    const auto pinnedId = kDrainContextModule;
    ASSERT_NO_FATAL_FAILURE(StartHost());
#endif
    ASSERT_TRUE(Host.CreateWorld(Context, Report));
    const auto* manager = detail::EngineHostTestAccess::Manager(Host);
    ASSERT_NE(manager, nullptr);
    const auto pinnedModule = manager->Find(pinnedId);
    if (!pinnedModule)
    {
        FAIL() << "待 Pin 的 Module 未接纳";
    }
    auto pin = manager->TryPinForBusiness(*pinnedModule);
    ASSERT_TRUE(pin);
    ASSERT_TRUE(Host.BeginShutdown(Report));
    const auto pending = Host.TryFinishShutdown(Report);
    ASSERT_FALSE(pending);
    EXPECT_EQ(pending.error(), module::MakeModuleError(module::ModuleError::kBusy));
    EXPECT_EQ(std::ranges::count(State->Events, Event::kWorldDestroy), 1);
    EXPECT_EQ(Host.GetState(), HostState::kClosing);
#ifdef PG_TEST_MODULAR
    EXPECT_EQ(pg::test::Observer().NativeUnloadCount(), 0U);
#endif

    pin->Release();
    ASSERT_TRUE(Host.TryFinishShutdown(Report));
    EXPECT_EQ(Host.GetState(), HostState::kClosed);
#ifdef PG_TEST_MODULAR
    EXPECT_EQ(pg::test::Observer().NativeUnloadCount(), 1U);
#endif
}
} // namespace pg::engine::test
