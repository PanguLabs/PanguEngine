#include "EngineHostTestFixture.h"
#include "HostServiceModule.h"
#include <EngineHost.h>
#include <EngineHostTestAccess.h>
#include <HostError.h>
#include <HostOperation.h>
#include <HostTestState.h>
#include <ModuleError.h>
#include <ModuleOperation.h>
#include <cstddef>
#include <gtest/gtest.h>
#include <memory>

namespace pg::engine::test
{
// 局部 Factory/Start 失败不得关闭宿主或其它 Scope，未交付 Scope 只由 Manager 回收。
TEST(EngineHostScope, LocalFailureKeepsOtherScopesReady)
{
    const auto state = std::make_shared<HostTestState>();
    SetHostTestState(state);
    EngineHost host;
    HostOperationReport report;
    const auto candidates = HostCandidates();
    ASSERT_TRUE(host.Start({.AdditionalResidentQueries = {},
                            .ResidentBindings = {},
                            .Modules = candidates,
                            .ModuleBindings = {},
                            .Logging = pg::engine::test::DisabledLogging()},
                           report));
    const auto stable = host.CreateContext(report);
    ASSERT_TRUE(stable);
    for (const bool factory : {true, false})
    {
        state->PollError = kStopError;
        if (factory)
        {
            // 第二条 Context recipe 失败，第一条已启动，确保真实 pending。
            state->OnFactory = [&]
            {
                if (state->FactoryCount % 2 == 1)
                {
                    state->FailFactory = kStartupError;
                }
            };
        }
        else
        {
            state->FailStart = kStartupError;
        }
        EXPECT_FALSE(host.CreateContext(report));
        EXPECT_EQ(host.GetState(), HostState::kReady);
        EXPECT_EQ(report.PrimaryError, kStartupError);
        EXPECT_FALSE(report.StartupFailure);
        if (!report.ModuleReport.has_value())
        {
            FAIL() << "缺少 Module 报告";
        }
        const auto failed = report.ModuleReport->Scope;
        EXPECT_EQ(report.ModuleReport->CleanupOwner, module::ModuleCleanupOwner::kManagerCleanup);
        const auto close = host.BeginCloseScope(failed, report);
        ASSERT_FALSE(close);
        EXPECT_EQ(close.error(), module::MakeModuleError(module::ModuleError::kScopeNotFound));
        EXPECT_FALSE(host.TryFinishCloseScope(failed, report));
        const auto stableAccess = AcquireContextValue(host, *stable);
        ASSERT_TRUE(stableAccess);
        EXPECT_TRUE(stableAccess->TryBorrow());
        state->OnFactory = {};
        state->FailFactory.reset();
        state->FailStart.reset();
        state->PollError.reset();
        ASSERT_TRUE(host.PollCleanup(report));
    }
    const auto world = host.CreateWorld(*stable, report);
    ASSERT_TRUE(world);
    const auto worldAccess = AcquireWorldValue(host, *world);
    ASSERT_TRUE(worldAccess);
    auto worldBorrow = worldAccess->TryBorrow();
    ASSERT_TRUE(worldBorrow);
    ASSERT_TRUE(host.BeginCloseScope(*stable, report));
    EXPECT_FALSE(host.TryFinishCloseScope(*stable, report));
    EXPECT_EQ(report.PrimaryError, module::MakeModuleError(module::ModuleError::kBusy));
    worldBorrow->Release();
    ASSERT_TRUE(host.TryFinishCloseScope(*stable, report));
}

TEST(EngineHostScope, SameReportReentryKeepsOuterResult)
{
    const auto state = std::make_shared<HostTestState>();
    SetHostTestState(state);
    EngineHost host;
    HostOperationReport report;
    const auto candidates = HostCandidates();
    ASSERT_TRUE(host.Start({.AdditionalResidentQueries = {},
                            .ResidentBindings = {},
                            .Modules = candidates,
                            .ModuleBindings = {},
                            .Logging = pg::engine::test::DisabledLogging()},
                           report));
    report.PrimaryError = kStopError;
    report.Stage = HostOperationStage::kBusinessAdmission;
    std::size_t entries{};
    const auto reenter = [&]
    {
        ++entries;
        const auto created = host.CreateContext(report);
        ASSERT_FALSE(created);
        EXPECT_EQ(created.error(), MakeHostError(HostError::kBusy));
        const auto world = host.CreateWorld(host.HostScope(), report);
        ASSERT_FALSE(world);
        EXPECT_EQ(world.error(), MakeHostError(HostError::kBusy));
        for (const auto status : {
                 host.BeginCloseScope(module::ScopeHandle(), report),
                 host.TryFinishCloseScope(module::ScopeHandle(), report),
                 host.PollCleanup(report),
             })
        {
            ASSERT_FALSE(status);
            EXPECT_EQ(status.error(), MakeHostError(HostError::kBusy));
        }
        const auto access = AcquireContextValue(host, {});
        ASSERT_FALSE(access);
        EXPECT_EQ(access.error(), MakeHostError(HostError::kBusy));
        EXPECT_EQ(report.PrimaryError, kStopError);
        EXPECT_EQ(report.Stage, HostOperationStage::kBusinessAdmission);
    };
    state->OnFactory = reenter;
    const auto context = host.CreateContext(report);
    ASSERT_TRUE(context);
    EXPECT_EQ(entries, 2U);
    EXPECT_EQ(report.State, HostState::kReady);
    EXPECT_EQ(report.Stage, HostOperationStage::kScopeOperation);
    EXPECT_FALSE(report.PrimaryError);
    if (!report.ModuleReport.has_value())
    {
        FAIL() << "缺少 Module 报告";
    }
    EXPECT_EQ(report.ModuleReport->Progress, module::ModuleOperationProgress::kScopeReady);
    state->OnFactory = {};
    ASSERT_TRUE(host.BeginCloseScope(*context, report));
    report.PrimaryError = kStopError;
    report.Stage = HostOperationStage::kBusinessAdmission;
    state->OnStop = reenter;
    ASSERT_TRUE(host.TryFinishCloseScope(*context, report));
    EXPECT_EQ(entries, 4U);
    EXPECT_FALSE(report.PrimaryError);
    EXPECT_EQ(report.Stage, HostOperationStage::kScopeOperation);
    if (!report.ModuleReport.has_value())
    {
        FAIL() << "缺少 Module 报告";
    }
    EXPECT_EQ(report.ModuleReport->Progress, module::ModuleOperationProgress::kScopeClosed);
    state->OnStop = {};
}

TEST(EngineHostScope, StateAndParentPreconditionsAreExplicit)
{
    SetHostTestState(std::make_shared<HostTestState>());
    EngineHost host;
    HostOperationReport report;
    const auto reject = [&]
    {
        const auto context = host.CreateContext(report);
        ASSERT_FALSE(context);
        EXPECT_EQ(context.error(), MakeHostError(HostError::kInvalidState));
        const auto world = host.CreateWorld(module::ScopeHandle(), report);
        ASSERT_FALSE(world);
        EXPECT_EQ(world.error(), MakeHostError(HostError::kInvalidState));
        for (const auto status : {
                 host.BeginCloseScope(module::ScopeHandle(), report),
                 host.TryFinishCloseScope(module::ScopeHandle(), report),
                 host.PollCleanup(report),
             })
        {
            ASSERT_FALSE(status);
            EXPECT_EQ(status.error(), MakeHostError(HostError::kInvalidState));
        }
        const auto access = AcquireContextValue(host, {});
        ASSERT_FALSE(access);
        EXPECT_EQ(access.error(), MakeHostError(HostError::kInvalidState));
    };
    reject();
    const auto candidates = HostCandidates();
    ASSERT_TRUE(host.Start({.AdditionalResidentQueries = {},
                            .ResidentBindings = {},
                            .Modules = candidates,
                            .ModuleBindings = {},
                            .Logging = pg::engine::test::DisabledLogging()},
                           report));
    const auto wrongParent = host.CreateWorld(host.HostScope(), report);
    ASSERT_FALSE(wrongParent);
    EXPECT_EQ(wrongParent.error(), module::MakeModuleError(module::ModuleError::kInvalidScope));
    EXPECT_FALSE(host.BeginCloseScope(host.HostScope(), report));
    ASSERT_TRUE(host.BeginShutdown(report));
    reject();
    EXPECT_EQ(host.GetState(), HostState::kClosing);
    ASSERT_TRUE(host.TryFinishShutdown(report));
    reject();
}

// 底层已进入整体关闭时，Scope 包装必须同步 Closing，且不能由 Poll 宣告 Closed。
TEST(EngineHostScope, ManagerShutdownReportSynchronizesClosing)
{
    for (int operation = 0; operation < 3; ++operation)
    {
        SetHostTestState(std::make_shared<HostTestState>());
        EngineHost host;
        HostOperationReport report;
        const auto candidates = HostCandidates();
        ASSERT_TRUE(host.Start({.AdditionalResidentQueries = {},
                                .ResidentBindings = {},
                                .Modules = candidates,
                                .ModuleBindings = {},
                                .Logging = pg::engine::test::DisabledLogging()},
                               report));
        const auto context = host.CreateContext(report);
        ASSERT_TRUE(context);
        const auto access = AcquireContextValue(host, *context);
        ASSERT_TRUE(access);
        auto borrow = access->TryBorrow();
        ASSERT_TRUE(borrow);
        module::ModuleOperationReport managerReport;
        ASSERT_TRUE(detail::EngineHostTestAccess::Manager(host)->BeginShutdown(managerReport));
        switch (operation)
        {
        case 0:
            EXPECT_TRUE(host.BeginCloseScope(*context, report));
            break;
        case 1:
            EXPECT_FALSE(host.TryFinishCloseScope(*context, report));
            break;
        default:
            EXPECT_FALSE(host.PollCleanup(report));
            break;
        }
        if (!report.ModuleReport.has_value())
        {
            FAIL() << "缺少 Module 报告";
        }
        EXPECT_EQ(report.ModuleReport->CleanupOwner, module::ModuleCleanupOwner::kManagerShutdown);
        EXPECT_EQ(host.GetState(), HostState::kClosing);
        EXPECT_EQ(report.State, HostState::kClosing);
        const auto denied = AcquireContextValue(host, *context);
        ASSERT_FALSE(denied);
        EXPECT_EQ(denied.error(), MakeHostError(HostError::kInvalidState));
        EXPECT_TRUE(host.InspectModules());
        borrow->Release();
        ASSERT_TRUE(host.TryFinishShutdown(report));
    }
}
} // namespace pg::engine::test
