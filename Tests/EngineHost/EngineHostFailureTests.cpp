#include "EngineHostTestFixture.h"
#include <EngineHost.h>
#include <EngineHostModule.h>
#include <HostOperation.h>
#include <HostServiceModule.h>
#include <HostStartupConfig.h>
#include <HostTestState.h>
#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <ModuleIdentity.h>
#include <ModuleManager.h>
#include <ModuleOperation.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <vector>
#ifdef PG_TEST_MODULAR
#include <HostServiceFixturePath.h>
#endif
#include <gtest/gtest.h>

namespace pg::engine::test
{
namespace
{
struct ResidentService
{
    std::shared_ptr<HostTestState> State;
};
// ScopeHooks 的固定签名接收可变服务引用。
// NOLINTNEXTLINE(misc-const-correctness)
Status ResidentStart(ResidentService& service) noexcept
{
    return service.State->FailStart ? Status{std::unexpected(*service.State->FailStart)} : Status{};
}
Status RegisterResident(module::RegistrationContext& context) noexcept
{
    const auto state = CurrentHostTestState();
    ++state->RegisterCount;
    if (state->FailRegister)
    {
        return std::unexpected(*state->FailRegister);
    }
    return context.DeclareFactory(kHostDeclaration, service::Scope::kHost,
                                  module::ScopeFactory(
                                      [state](module::ScopeBuildContext& build) noexcept -> Status
                                      {
                                          ++state->FactoryCount;
                                          const auto result =
                                              build.Own(std::make_unique<ResidentService>(ResidentService{state}),
                                                        module::ScopeHooks<ResidentService>{.Start = ResidentStart});
                                          return result ? Status{} : Status{std::unexpected(result.error())};
                                      }));
}
const module::ModuleDefinition* QueryResident() noexcept
{
    ++CurrentHostTestState()->QueryCount;
    static constexpr std::array kDependencies{
        module::ModuleDependency{.Id = kEngineHostModuleId, .Version = {.Major = 1}},
    };
    static constexpr module::ModuleDefinition kDefinition{
        .Descriptor =
            {
                .Id = kModuleId,
                .Version = {.Major = 1},
                .Dependencies = kDependencies,
                .DisplayName = "HostResidentFixture",
                .BuildIdentity = {},
            },
        .RegisterModule = RegisterResident,
    };
    return &kDefinition;
}
const module::ModuleDefinition* QueryInvalid() noexcept
{
    ++CurrentHostTestState()->QueryCount;
    return nullptr;
}
} // namespace

// 六阶段表驱动：提前失败必须阻止后续注册/激活，报告保留实际阶段及错误。
TEST(EngineHostFailure, ResidentAndBusinessStagesStopAtFirstError)
{
    constexpr std::array kStages{
        HostOperationStage::kResidentAdmission,    HostOperationStage::kResidentRegistration,
        HostOperationStage::kResidentActivation,   HostOperationStage::kBusinessAdmission,
        HostOperationStage::kBusinessRegistration, HostOperationStage::kBusinessActivation,
    };
    for (const auto stage : kStages)
    {
        SCOPED_TRACE(static_cast<int>(stage));
        const auto state = std::make_shared<HostTestState>();
        SetHostTestState(state);
        const bool resident = stage <= HostOperationStage::kResidentActivation;
        const bool admission =
            stage == HostOperationStage::kResidentAdmission || stage == HostOperationStage::kBusinessAdmission;
        const bool registration =
            stage == HostOperationStage::kResidentRegistration || stage == HostOperationStage::kBusinessRegistration;
        if (registration)
        {
            state->FailRegister = kStartupError;
        }
        if (!admission && !registration)
        {
            state->FailStart = kStartupError;
        }
        const std::array residents{admission ? QueryInvalid : QueryResident};
        auto candidates = HostCandidates();
        if (!resident && admission)
        {
#ifdef PG_TEST_MODULAR
            candidates.front() = module::SharedModuleCandidate{
                .ExpectedId = module::ModuleId{.Bytes = {std::byte{0xe5}}},
                .Path = kHostServiceFixturePath,
            };
#else
            candidates.front() = module::StaticModuleCandidate{QueryInvalid};
#endif
        }
        HostStartupConfig config;
        config.Modules = candidates;
        if (resident)
        {
            config.AdditionalResidentQueries = residents;
        }
        EngineHost host;
        HostOperationReport report;
        const auto started = host.Start(config, report);
        ASSERT_FALSE(started);
        EXPECT_EQ(report.Stage, stage);
        EXPECT_EQ(report.PrimaryError, started.error());
        ASSERT_TRUE(report.StartupFailure.has_value());
        EXPECT_EQ(report.StartupFailure->Stage, stage);
        EXPECT_EQ(report.StartupFailure->Cause, started.error());
        ASSERT_TRUE(report.ModuleReport);
        ASSERT_TRUE(report.StartupFailure->ModuleReport);
        EXPECT_EQ(report.StartupFailure->ModuleReport->PrimaryError, started.error());
        if (stage != HostOperationStage::kResidentAdmission)
        {
            EXPECT_EQ(report.ModuleReport->CleanupOwner, module::ModuleCleanupOwner::kManagerShutdown);
            EXPECT_EQ(report.ModuleReport->Progress, module::ModuleOperationProgress::kUnloaded);
            EXPECT_FALSE(report.ModuleReport->CleanupError);
            EXPECT_NE(report.StartupFailure->ModuleReport->Progress, module::ModuleOperationProgress::kUnloaded);
        }
        else
        {
            EXPECT_EQ(report.ModuleReport->Progress, report.StartupFailure->ModuleReport->Progress);
            EXPECT_EQ(report.ModuleReport->CleanupOwner, report.StartupFailure->ModuleReport->CleanupOwner);
        }
        if (!admission)
        {
            EXPECT_EQ(started.error(), kStartupError);
        }
        else if (resident)
        {
            EXPECT_EQ(started.error(), module::MakeModuleError(module::ModuleError::kInvalidDefinition));
        }
        else
        {
#ifdef PG_TEST_MODULAR
            EXPECT_EQ(started.error(), module::MakeModuleError(module::ModuleError::kInvalidDescriptor));
            EXPECT_EQ(state->NativeUnloadCount, 1U);
#else
            EXPECT_EQ(started.error(), module::MakeModuleError(module::ModuleError::kInvalidDefinition));
#endif
        }
        EXPECT_EQ(state->RegisterCount, admission ? 0U : 1U);
        EXPECT_EQ(state->FactoryCount, admission || registration ? 0U : 1U);
        if (resident)
        {
            // 业务候选已配置；唯一一次 Query 来自常驻候选，不能继续接纳业务。
            EXPECT_EQ(state->QueryCount, 1U);
            EXPECT_EQ(state->NativeUnloadCount, 0U);
        }
        EXPECT_EQ(host.GetState(), HostState::kClosed);
        EXPECT_TRUE(host.InspectModules()->empty());
        EXPECT_TRUE(host.InspectDeclarations()->empty());
        EXPECT_FALSE(host.Start(config, report));
    }
}

TEST(EngineHostFailure, StartupBusyIsFailure)
{
    const auto state = std::make_shared<HostTestState>();
    SetHostTestState(state);
    state->FailStart = module::MakeModuleError(module::ModuleError::kBusy);
    EngineHost host;
    HostOperationReport report;
    const auto candidates = HostCandidates();
    const auto started = host.Start(
        {.AdditionalResidentQueries = {}, .ResidentBindings = {}, .Modules = candidates, .ModuleBindings = {}}, report);
    ASSERT_FALSE(started);
    EXPECT_EQ(started.error(), *state->FailStart);
    EXPECT_EQ(report.Stage, HostOperationStage::kBusinessActivation);
    EXPECT_EQ(host.GetState(), HostState::kClosed);
    state->FailStart.reset();
    EXPECT_FALSE(host.Start(
        {.AdditionalResidentQueries = {}, .ResidentBindings = {}, .Modules = candidates, .ModuleBindings = {}},
        report));
    EXPECT_EQ(state->FactoryCount, 1U);
}

TEST(EngineHostFailure, ResidentLimitChecksBeforeQuery)
{
    const auto state = std::make_shared<HostTestState>();
    SetHostTestState(state);
    // 253 项允许进入 Query（此处故意非法），254 项必须在调用前拒绝。
    for (const std::size_t count : {253U, 254U})
    {
        state->Reset();
        const std::vector<module::QueryModuleFn> residents(count, QueryInvalid);
        EngineHost host;
        HostOperationReport report;
        const auto result = host.Start(
            {.AdditionalResidentQueries = residents, .ResidentBindings = {}, .Modules = {}, .ModuleBindings = {}},
            report);
        ASSERT_FALSE(result);
        EXPECT_EQ(report.Stage, HostOperationStage::kResidentAdmission);
        if (count == 254U)
        {
            EXPECT_EQ(result.error(), module::MakeModuleError(module::ModuleError::kLimitExceeded));
            EXPECT_EQ(state->QueryCount, 0U);
            EXPECT_FALSE(report.ModuleReport);
            ASSERT_TRUE(report.StartupFailure);
            if (report.StartupFailure)
            {
                EXPECT_FALSE(report.StartupFailure->ModuleReport);
            }
        }
        else
        {
            EXPECT_GT(state->QueryCount, 0U);
        }
        EXPECT_EQ(host.GetState(), HostState::kClosed);
    }
}

TEST(EngineHostFailure, BusinessLimitChecksBeforeQuery)
{
    const auto state = std::make_shared<HostTestState>();
    SetHostTestState(state);
    const std::vector<module::ModuleLoadCandidate> candidates(254, HostCandidates().front());
    EngineHost host;
    HostOperationReport report;
    HostStartupConfig config;
    config.Modules = candidates;
    const auto result = host.Start(config, report);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), module::MakeModuleError(module::ModuleError::kLimitExceeded));
    EXPECT_EQ(report.Stage, HostOperationStage::kBusinessAdmission);
    EXPECT_EQ(report.PrimaryError, result.error());
    ASSERT_TRUE(report.StartupFailure);
    if (report.StartupFailure)
    {
        EXPECT_EQ(report.StartupFailure->Cause, result.error());
        EXPECT_FALSE(report.StartupFailure->ModuleReport);
    }
    ASSERT_TRUE(report.ModuleReport);
    if (report.ModuleReport)
    {
        EXPECT_EQ(report.ModuleReport->CleanupOwner, module::ModuleCleanupOwner::kManagerShutdown);
        EXPECT_EQ(report.ModuleReport->Progress, module::ModuleOperationProgress::kUnloaded);
        EXPECT_FALSE(report.ModuleReport->CleanupError);
    }
    EXPECT_EQ(state->QueryCount, 0U);
    EXPECT_EQ(state->RegisterCount, 0U);
    EXPECT_EQ(host.GetState(), HostState::kClosed);
    EXPECT_TRUE(host.InspectModules()->empty());
}

#ifdef PG_TEST_MODULAR
TEST(EngineHostFailure, InvalidBusinessBatchReleasesAlreadyLoadedCandidate)
{
    const auto state = std::make_shared<HostTestState>();
    SetHostTestState(state);
    const std::array candidates{
        HostCandidates().front(),
        module::ModuleLoadCandidate{module::SharedModuleCandidate{
            .ExpectedId = module::ModuleId{.Bytes = {std::byte{0xe5}}},
            .Path = std::filesystem::path(kHostServiceFixturePath).parent_path() / "MissingHostFixture.dll",
        }},
    };
    EngineHost host;
    HostStartupConfig config;
    config.Modules = candidates;
    HostOperationReport report;
    const auto result = host.Start(config, report);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().Domain(), module::kModuleErrorDomain);
    EXPECT_EQ(result.error().Code(), static_cast<std::uint32_t>(module::ModuleError::kLibraryOpenFailed));
#ifdef _WIN32
    EXPECT_TRUE(result.error().Native());
#else
    // POSIX Loader 只保留确定错误，不借用 dlerror 文本或伪造原生数值。
    EXPECT_FALSE(result.error().Native());
#endif
    EXPECT_EQ(report.PrimaryError, result.error());
    EXPECT_EQ(report.Stage, HostOperationStage::kBusinessAdmission);
    EXPECT_EQ(state->QueryCount, 1U);
    EXPECT_EQ(state->RegisterCount, 0U);
    EXPECT_EQ(state->FactoryCount, 0U);
    EXPECT_EQ(state->NativeUnloadCount, 1U);
    EXPECT_EQ(host.GetState(), HostState::kClosed);
    EXPECT_TRUE(host.InspectModules()->empty());
}
#endif
} // namespace pg::engine::test
