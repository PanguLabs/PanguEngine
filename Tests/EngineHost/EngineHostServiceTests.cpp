#include "EngineHostTestFixture.h"
#include "HostServiceModule.h"
#include <EngineHost.h>
#include <EngineHostModule.h>
#include <HostError.h>
#include <HostOperation.h>
#include <HostServiceAccess.h>
#include <HostStartupConfig.h>
#include <HostTestState.h>
#include <ModuleDeclaration.h>
#include <ModuleError.h>
#include <ModuleOperation.h>
#include <Result.h>
#include <ScopeAccess.h>
#include <ScopeIdentity.h>
#include <ScopeResourceAccess.h>
#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <utility>
#include <vector>

namespace pg::engine::test
{
namespace
{
using Kind = service::Scope;
using Access = detail::HostResourceAccess;
module::detail::ScopeResourceQuery Query(module::ScopeHandle scope, module::DeclarationId declaration, Kind kind)
{
    return {.Scope = scope, .Owner = kModuleId, .Declaration = declaration, .Kind = kind};
}
template <class T = HostServiceValue>
void ExpectError(const EngineHost& host, const module::detail::ScopeResourceQuery& query, module::ModuleError error)
{
    const auto access = Access::Acquire<T>(host, query);
    ASSERT_FALSE(access);
    EXPECT_EQ(access.error(), module::MakeModuleError(error));
}
void ExpectValue(const Result<module::ScopeAccess<HostServiceValue>>& access, int value)
{
    ASSERT_TRUE(access);
    const auto borrow = access->TryBorrow();
    ASSERT_TRUE(borrow);
    EXPECT_EQ((*borrow)->Value, value);
}
} // namespace

// 混用 Host、Scope 或复用单例服务时，值与同时存活对象的地址断言必须失败。
TEST(EngineHostService, TwoHostsAndScopesStayIndependent)
{
    EngineHost first;
    EngineHost second;
    HostOperationReport report;
    const auto candidates = HostCandidates();
    HostStartupConfig config;
    config.Modules = candidates;
    SetHostTestState(std::make_shared<HostTestState>());
    ASSERT_TRUE(first.Start(config, report));
    SetHostTestState(std::make_shared<HostTestState>());
    ASSERT_TRUE(second.Start(config, report));
    std::vector<module::ScopeBorrow<HostServiceValue>> borrows;
    for (auto* host : {&first, &second})
    {
        for (int contextIndex = 0; contextIndex < 2; ++contextIndex)
        {
            const auto context = host->CreateContext(report);
            ASSERT_TRUE(context);
            const auto access = AcquireContextValue(*host, *context);
            ASSERT_TRUE(access);
            auto borrow = access->TryBorrow();
            ASSERT_TRUE(borrow);
            EXPECT_EQ((*borrow)->Value, 2);
            borrows.push_back(std::move(*borrow));
            for (int worldIndex = 0; worldIndex < 2; ++worldIndex)
            {
                const auto world = host->CreateWorld(*context, report);
                ASSERT_TRUE(world);
                const auto worldAccess = AcquireWorldValue(*host, *world);
                ASSERT_TRUE(worldAccess);
                auto worldBorrow = worldAccess->TryBorrow();
                ASSERT_TRUE(worldBorrow);
                EXPECT_EQ((*worldBorrow)->Value, 3);
                borrows.push_back(std::move(*worldBorrow));
            }
        }
    }
    for (std::size_t firstIndex = 0; firstIndex < borrows.size(); ++firstIndex)
    {
        for (std::size_t secondIndex = firstIndex + 1; secondIndex < borrows.size(); ++secondIndex)
        {
            EXPECT_NE(borrows.at(firstIndex).operator->(), borrows.at(secondIndex).operator->());
        }
    }
    borrows.clear();
}

// 元数据、无服务 Module、同类型不同声明、错误类型及跨域身份均须精确判定。
TEST(EngineHostService, MetadataAndIdentityAreExact)
{
    const auto state = std::make_shared<HostTestState>();
    SetHostTestState(state);
    EngineHost host;
    EngineHost other;
    HostOperationReport report;
    const auto candidates = HostCandidates();
    ASSERT_TRUE(host.Start(
        {.AdditionalResidentQueries = {}, .ResidentBindings = {}, .Modules = candidates, .ModuleBindings = {}},
        report));
    SetHostTestState(std::make_shared<HostTestState>());
    ASSERT_TRUE(other.Start(
        {.AdditionalResidentQueries = {}, .ResidentBindings = {}, .Modules = candidates, .ModuleBindings = {}},
        report));
    const auto context = host.CreateContext(report);
    const auto foreign = other.CreateContext(report);
    ASSERT_TRUE(context);
    ASSERT_TRUE(foreign);
    const auto made = state->FactoryCount;
    ExpectValue(AcquireContextValue(host, *context), 2);
    ExpectValue(Access::Acquire<HostServiceValue>(host, Query(*context, kSecondContextDeclaration, Kind::kContext)), 4);
    ExpectError(host, Query(host.HostScope(), kMetadataDeclaration, Kind::kHost), module::ModuleError::kNotFound);
    auto query = Query(*context, kContextDeclaration, Kind::kContext);
    query.Owner = kEngineHostModuleId;
    ExpectError(host, query, module::ModuleError::kNotFound);
    query.Owner = kModuleId;
    ExpectError<int>(host, query, module::ModuleError::kTypeMismatch);
    query.Kind = Kind::kWorld;
    ExpectError(host, query, module::ModuleError::kInvalidScope);
    // 故意构造不受支持的枚举值，验证公开入口的输入校验。
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    query.Kind = static_cast<Kind>(255);
    ExpectError(host, query, module::ModuleError::kInvalidScope);
    query = Query(*foreign, kContextDeclaration, Kind::kContext);
    ExpectError(host, query, module::ModuleError::kScopeNotFound);
    query.Scope = *context;
    ++query.Scope.Generation;
    ExpectError(host, query, module::ModuleError::kScopeNotFound);
    query.Scope = {};
    ExpectError(host, query, module::ModuleError::kScopeNotFound);
    EXPECT_EQ(state->FactoryCount, made);
    EXPECT_FALSE(report.StartupFailure);
}

TEST(EngineHostService, NoImplicitAncestorLookup)
{
    SetHostTestState(std::make_shared<HostTestState>());
    EngineHost host;
    HostOperationReport report;
    const auto candidates = HostCandidates();
    ASSERT_TRUE(host.Start(
        {.AdditionalResidentQueries = {}, .ResidentBindings = {}, .Modules = candidates, .ModuleBindings = {}},
        report));
    const auto context = host.CreateContext(report);
    ASSERT_TRUE(context);
    const auto world = host.CreateWorld(*context, report);
    ASSERT_TRUE(world);
    const auto wrong = AcquireContextValue(host, *world);
    ASSERT_FALSE(wrong);
    EXPECT_EQ(wrong.error(), module::MakeModuleError(module::ModuleError::kInvalidScope));
    ExpectError(host, Query(*world, kContextDeclaration, Kind::kWorld), module::ModuleError::kNotFound);
    ExpectError(host, Query(*context, kHostDeclaration, Kind::kContext), module::ModuleError::kNotFound);
    ExpectValue(AcquireContextValue(host, *context), 2);
}

// 失败装配回收复用同槽后，失败句柄、已关闭令牌都不能指向新对象。
TEST(EngineHostService, FailedScopeReuseDoesNotAlias)
{
    const auto state = std::make_shared<HostTestState>();
    SetHostTestState(state);
    EngineHost host;
    HostOperationReport report;
    const auto candidates = HostCandidates();
    ASSERT_TRUE(host.Start(
        {.AdditionalResidentQueries = {}, .ResidentBindings = {}, .Modules = candidates, .ModuleBindings = {}},
        report));
    const auto stable = host.CreateContext(report);
    const auto old = host.CreateContext(report);
    ASSERT_TRUE(stable);
    ASSERT_TRUE(old);
    const auto token = AcquireContextValue(host, *old);
    ASSERT_TRUE(token);
    ASSERT_TRUE(host.BeginCloseScope(*old, report));
    ASSERT_TRUE(host.TryFinishCloseScope(*old, report));
    state->OnFactory = [&]
    {
        if (state->FactoryCount % 2 == 1)
        {
            state->FailFactory = kStartupError;
        }
    };
    state->PollError = kStopError;
    const auto failed = host.CreateContext(report);
    ASSERT_FALSE(failed);
    if (!report.ModuleReport.has_value())
    {
        FAIL() << "缺少 Module 报告";
    }
    const auto undelivered = report.ModuleReport->Scope;
    EXPECT_EQ(undelivered.Slot, old->Slot);
    EXPECT_NE(undelivered.Generation, old->Generation);
    ExpectError(host, Query(undelivered, kContextDeclaration, Kind::kContext), module::ModuleError::kNotReady);
    state->OnFactory = {};
    state->FailFactory.reset();
    state->PollError.reset();
    ASSERT_TRUE(host.PollCleanup(report));
    const auto replacement = host.CreateContext(report);
    ASSERT_TRUE(replacement);
    EXPECT_EQ(replacement->Slot, undelivered.Slot);
    EXPECT_NE(replacement->Generation, undelivered.Generation);
    ExpectError(host, Query(undelivered, kContextDeclaration, Kind::kContext), module::ModuleError::kScopeNotFound);
    ExpectError(host, Query(*old, kContextDeclaration, Kind::kContext), module::ModuleError::kScopeNotFound);
    const auto denied = token->TryBorrow();
    ASSERT_FALSE(denied);
    EXPECT_EQ(denied.error(), module::MakeModuleError(module::ModuleError::kNotReady));
    ExpectValue(AcquireContextValue(host, *stable), 2);
    ExpectValue(AcquireContextValue(host, *replacement), 2);
}
// 查询拒绝不参与启动诊断或清理状态写入。
TEST(EngineHostService, QueryFailurePreservesStartupFailure)
{
    const auto state = std::make_shared<HostTestState>();
    SetHostTestState(state);
    state->FailStart = kStartupError;
    state->PollError = kStopError;
    EngineHost host;
    HostOperationReport report;
    const auto candidates = HostCandidates();
    EXPECT_FALSE(host.Start(
        {.AdditionalResidentQueries = {}, .ResidentBindings = {}, .Modules = candidates, .ModuleBindings = {}},
        report));
    if (!report.StartupFailure.has_value())
    {
        FAIL() << "缺少启动首错";
    }
    const auto startup = report.StartupFailure;
    const auto primary = report.PrimaryError;
    const auto stage = report.Stage;
    const auto access = AcquireContextValue(host, {});
    ASSERT_FALSE(access);
    EXPECT_EQ(access.error(), MakeHostError(HostError::kInvalidState));
    EXPECT_EQ(report.PrimaryError, primary);
    EXPECT_EQ(report.Stage, stage);
    EXPECT_FALSE(host.TryFinishShutdown(report));
    if (!report.StartupFailure.has_value())
    {
        FAIL() << "缺少启动首错";
    }
    if (!startup.has_value())
    {
        FAIL() << "缺少启动首错快照";
    }
    EXPECT_EQ(report.StartupFailure->Stage, startup->Stage);
    EXPECT_EQ(report.StartupFailure->Cause, startup->Cause);
    if (!report.StartupFailure->ModuleReport.has_value())
    {
        FAIL() << "缺少启动 Module 报告";
    }
    if (!startup->ModuleReport.has_value())
    {
        FAIL() << "缺少启动 Module 报告快照";
    }
    EXPECT_EQ(report.StartupFailure->ModuleReport->OriginalError, startup->ModuleReport->OriginalError);
    state->PollError.reset();
    ASSERT_TRUE(host.TryFinishShutdown(report));
}
} // namespace pg::engine::test
