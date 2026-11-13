#include "EngineHostTestFixture.h"
#include <EngineHost.h>
#include <EngineHostTestAccess.h>
#include <Error.h>
#include <HostError.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <HostTestState.h>
#include <ModuleError.h>
#include <ModuleOperation.h>
#include <ModuleTestAccess.h>
#include <cstddef>
#include <gtest/gtest.h>
#include <memory>

namespace pg::engine::test
{
// 外部 Pin 和一次原生释放失败必须分别保留 Manager，直至真实重试成功。
TEST(Shutdown, NativeCloseFailureRetainsManager)
{
    const auto state = std::make_shared<HostTestState>();
    SetHostTestState(state);
    EngineHost host;
    HostOperationReport report;
    const auto candidates = HostCandidates();
    HostStartupConfig config;
    config.Logging = pg::engine::test::DisabledLogging();
    config.Modules = candidates;
    ASSERT_TRUE(host.Start(config, report));
    auto* manager = detail::EngineHostTestAccess::Manager(host);
    ASSERT_NE(manager, nullptr);
    const auto handle = manager->Modules().back();
    auto pin = manager->TryPinForBusiness(handle);
    ASSERT_TRUE(pin);
    ASSERT_TRUE(host.BeginShutdown(report));
    EXPECT_EQ(host.GetState(), HostState::kClosing);
    EXPECT_FALSE(host.TryFinishShutdown(report));
    EXPECT_EQ(report.PrimaryError, module::MakeModuleError(module::ModuleError::kBusy));
    EXPECT_EQ(detail::EngineHostTestAccess::Manager(host), manager);
    EXPECT_EQ(state->NativeUnloadCount, 0U);
    pin->Release();
#ifdef PG_TEST_MODULAR
    module::detail::ModuleManagerTestAccess::FailNextNativeClose(*manager, handle);
    EXPECT_FALSE(host.TryFinishShutdown(report));
    EXPECT_EQ(report.PrimaryError, module::MakeModuleError(module::ModuleError::kLibraryCloseFailed));
    EXPECT_EQ(host.GetState(), HostState::kClosing);
    EXPECT_EQ(detail::EngineHostTestAccess::Manager(host), manager);
    EXPECT_TRUE(host.InspectModules());
    EXPECT_EQ(state->NativeUnloadCount, 0U);
#endif
    ASSERT_TRUE(host.TryFinishShutdown(report));
    EXPECT_EQ(host.GetState(), HostState::kClosed);
    EXPECT_EQ(detail::EngineHostTestAccess::Manager(host), nullptr);
    EXPECT_EQ(state->DestroyCount, 1U);
#ifdef PG_TEST_MODULAR
    EXPECT_EQ(state->NativeUnloadCount, 1U);
#endif
}

// 跨轮清理错误不能覆盖启动首错；旧报告销毁也不能丢失拥有型快照。
TEST(Shutdown, NonBusyStopFailurePreservesStartupFailure)
{
    const auto state = std::make_shared<HostTestState>();
    SetHostTestState(state);
    state->FailStart = kStartupError;
    state->PollError = kStopError;
    EngineHost host;
    const auto candidates = HostCandidates();
    {
        HostOperationReport report;
        const auto started = host.Start({.AdditionalResidentQueries = {},
                                         .ResidentBindings = {},
                                         .Modules = candidates,
                                         .ModuleBindings = {},
                                         .Logging = pg::engine::test::DisabledLogging()},
                                        report);
        ASSERT_FALSE(started);
        EXPECT_EQ(started.error(), kStartupError);
        EXPECT_EQ(report.PrimaryError, kStartupError);
        ASSERT_TRUE(report.StartupFailure.has_value());
        if (report.StartupFailure)
        {
            EXPECT_EQ(report.StartupFailure->Cause, kStartupError);
        }
        EXPECT_EQ(host.GetState(), HostState::kClosing);
        EXPECT_TRUE(report.ModuleReport.has_value());
        if (report.ModuleReport)
        {
            EXPECT_EQ(report.ModuleReport->CleanupOwner, module::ModuleCleanupOwner::kManagerShutdown);
            EXPECT_EQ(report.ModuleReport->Progress, module::ModuleOperationProgress::kCleanupPending);
            EXPECT_EQ(report.ModuleReport->CleanupError, kStopError);
        }
        if (report.StartupFailure)
        {
            EXPECT_TRUE(report.StartupFailure->ModuleReport.has_value());
            if (report.StartupFailure->ModuleReport)
            {
                EXPECT_EQ(report.StartupFailure->ModuleReport->CleanupOwner, module::ModuleCleanupOwner::kCallerUnload);
                EXPECT_EQ(report.StartupFailure->ModuleReport->Progress,
                          module::ModuleOperationProgress::kCleanupPending);
                EXPECT_EQ(report.StartupFailure->ModuleReport->CleanupError, kStopError);
                EXPECT_EQ(report.StartupFailure->ModuleReport->OriginalError, kStartupError);
            }
        }
        EXPECT_EQ(state->DestroyCount, 0U);
    }
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        HostOperationReport report;
        const auto finished = host.TryFinishShutdown(report);
        EXPECT_FALSE(finished);
        if (!finished)
        {
            EXPECT_EQ(finished.error(), kStopError);
        }
        ASSERT_TRUE(report.StartupFailure.has_value());
        if (report.StartupFailure)
        {
            EXPECT_EQ(report.StartupFailure->Cause, pg::MakeError(pg::ErrorDomainId::Constant<0x741>(), 101));
        }
        EXPECT_EQ(host.GetState(), pg::engine::HostState::kClosing);
        EXPECT_EQ(report.PrimaryError, pg::MakeError(pg::ErrorDomainId::Constant<0x741>(), 102));
        EXPECT_EQ(state->DestroyCount, 0U);
        EXPECT_EQ(state->NativeUnloadCount, 0U);
    }
    state->PollError.reset();
    HostOperationReport report;
    EXPECT_TRUE(host.TryFinishShutdown(report));
    EXPECT_EQ(host.GetState(), HostState::kClosed);
    EXPECT_FALSE(report.PrimaryError);
    ASSERT_TRUE(report.StartupFailure.has_value());
    if (report.StartupFailure)
    {
        EXPECT_EQ(report.StartupFailure->Cause, kStartupError);
    }
    EXPECT_EQ(state->DestroyCount, 1U);
}

// 重入即使复用外层 report，也只能返回 Busy，不能发布半成品报告。
TEST(Shutdown, SameReportReentryKeepsOuterResult)
{
    const auto state = std::make_shared<HostTestState>();
    SetHostTestState(state);
    EngineHost host;
    HostOperationReport report;
    report.PrimaryError = kStopError;
    report.Stage = HostOperationStage::kScopeOperation;
    std::size_t reentries{};
    state->OnQuery = [&]
    {
        ++reentries;
        HostStartupConfig config;
        config.Logging = pg::engine::test::DisabledLogging();
        const auto nested = host.Start(config, report);
        ASSERT_FALSE(nested);
        EXPECT_EQ(nested.error(), MakeHostError(HostError::kBusy));
        const auto finish = host.TryFinishShutdown(report);
        ASSERT_FALSE(finish);
        EXPECT_EQ(finish.error(), MakeHostError(HostError::kBusy));
        EXPECT_EQ(report.PrimaryError, kStopError);
        EXPECT_EQ(report.Stage, HostOperationStage::kScopeOperation);
    };
    state->OnFactory = [&]
    {
        ++reentries;
        const auto nested = host.BeginShutdown(report);
        EXPECT_FALSE(nested);
        if (!nested)
        {
            EXPECT_EQ(nested.error(), MakeHostError(HostError::kBusy));
        }
        EXPECT_EQ(report.PrimaryError, kStopError);
        EXPECT_EQ(report.Stage, HostOperationStage::kScopeOperation);
        EXPECT_EQ(host.GetState(), HostState::kStarting);
        EXPECT_FALSE(host.InspectModules());
        EXPECT_FALSE(host.InspectDeclarations());
    };
    const auto candidates = HostCandidates();
    EXPECT_TRUE(host.Start({.AdditionalResidentQueries = {},
                            .ResidentBindings = {},
                            .Modules = candidates,
                            .ModuleBindings = {},
                            .Logging = pg::engine::test::DisabledLogging()},
                           report));
    EXPECT_EQ(reentries, 2U);
    EXPECT_EQ(report.State, HostState::kReady);
    EXPECT_FALSE(report.PrimaryError);
    state->OnFactory = {};
    state->OnQuery = {};
    EXPECT_TRUE(host.BeginShutdown(report));
    const auto stage = report.Stage;
    state->OnNativeUnload = [&]
    {
        ++reentries;
        const auto nested = host.TryFinishShutdown(report);
        EXPECT_FALSE(nested);
        if (!nested)
        {
            EXPECT_EQ(nested.error(), MakeHostError(HostError::kBusy));
        }
        EXPECT_EQ(report.State, HostState::kClosing);
        EXPECT_EQ(report.Stage, stage);
        EXPECT_FALSE(report.PrimaryError);
    };
    EXPECT_TRUE(host.TryFinishShutdown(report));
#ifdef PG_TEST_MODULAR
    EXPECT_EQ(reentries, 3U);
    EXPECT_EQ(state->NativeUnloadCount, 1U);
#endif
    EXPECT_EQ(report.State, HostState::kClosed);
    EXPECT_FALSE(report.PrimaryError);
    state->OnNativeUnload = {};
}

TEST(Shutdown, RepeatedOperationsNeverRestartOrRequery)
{
    const auto state = std::make_shared<HostTestState>();
    SetHostTestState(state);
    EngineHost host;
    HostOperationReport report;
    EXPECT_FALSE(host.TryFinishShutdown(report));
    const auto candidates = HostCandidates();
    ASSERT_TRUE(host.Start({.AdditionalResidentQueries = {},
                            .ResidentBindings = {},
                            .Modules = candidates,
                            .ModuleBindings = {},
                            .Logging = pg::engine::test::DisabledLogging()},
                           report));
    const auto queries = state->QueryCount;
    EXPECT_FALSE(host.Start({.AdditionalResidentQueries = {},
                             .ResidentBindings = {},
                             .Modules = candidates,
                             .ModuleBindings = {},
                             .Logging = pg::engine::test::DisabledLogging()},
                            report));
    EXPECT_EQ(host.GetState(), HostState::kReady);
    ASSERT_TRUE(host.BeginShutdown(report));
    EXPECT_EQ(host.GetState(), HostState::kClosing);
    EXPECT_TRUE(host.BeginShutdown(report));
    EXPECT_FALSE(host.Start({.AdditionalResidentQueries = {},
                             .ResidentBindings = {},
                             .Modules = candidates,
                             .ModuleBindings = {},
                             .Logging = pg::engine::test::DisabledLogging()},
                            report));
    ASSERT_TRUE(host.TryFinishShutdown(report));
    EXPECT_TRUE(host.TryFinishShutdown(report));
    EXPECT_TRUE(host.BeginShutdown(report));
    EXPECT_FALSE(host.Start({.AdditionalResidentQueries = {},
                             .ResidentBindings = {},
                             .Modules = candidates,
                             .ModuleBindings = {},
                             .Logging = pg::engine::test::DisabledLogging()},
                            report));
    EXPECT_EQ(state->QueryCount, queries);
    EXPECT_EQ(state->FactoryCount, 1U);
    EXPECT_EQ(state->DestroyCount, 1U);
    EXPECT_EQ(host.GetState(), HostState::kClosed);
}

// 后启动的 Host 替换当前观测状态，不能改变已注册服务的停止和销毁归属。
TEST(Shutdown, RegistrationsKeepIndependentObservationState)
{
    const auto first = std::make_shared<HostTestState>();
    const auto second = std::make_shared<HostTestState>();
    EngineHost firstHost;
    EngineHost secondHost;
    HostOperationReport report;
    const auto candidates = HostCandidates();
    HostStartupConfig config;
    config.Logging = pg::engine::test::DisabledLogging();
    config.Modules = candidates;
    SetHostTestState(first);
    ASSERT_TRUE(firstHost.Start(config, report));
    SetHostTestState(second);
    ASSERT_TRUE(secondHost.Start(config, report));
    first->PollError = kStopError;
    ASSERT_TRUE(firstHost.BeginShutdown(report));
    EXPECT_FALSE(firstHost.TryFinishShutdown(report));
    EXPECT_EQ(report.PrimaryError, kStopError);
    EXPECT_EQ(first->DestroyCount, 0U);
    EXPECT_EQ(second->PollCount, 0U);
    EXPECT_EQ(second->DestroyCount, 0U);
    EXPECT_EQ(secondHost.GetState(), HostState::kReady);
    first->PollError.reset();
    ASSERT_TRUE(firstHost.TryFinishShutdown(report));
    EXPECT_EQ(first->DestroyCount, 1U);
    EXPECT_EQ(second->DestroyCount, 0U);
    ASSERT_TRUE(secondHost.BeginShutdown(report));
    ASSERT_TRUE(secondHost.TryFinishShutdown(report));
    EXPECT_EQ(second->DestroyCount, 1U);
}
} // namespace pg::engine::test
