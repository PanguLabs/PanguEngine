#include "../Log/ConsumerPause.h"
#include <EngineHost.h>
#include <EngineHostModule.h>
#include <EngineHostTestAccess.h>
#include <Error.h>
#include <FailureContext.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <Log.h>
#include <LogConfig.h>
#include <LogQueue.h>
#include <LogReports.h>
#include <LogSystem.h>
#include <LogTestAccess.h>
#include <LogTypes.h>
#include <LogWriter.h>
#include <ModuleDeclaration.h>
#include <ModuleDescriptor.h>
#include <ModuleIdentity.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>
#include <WriterControl.h>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace pg::engine::test
{
namespace
{
const auto kStartupError = MakeError(ErrorDomainId::Constant<0x742>(), 1);
const auto kCleanupError = MakeError(ErrorDomainId::Constant<0x742>(), 2);
constexpr module::ModuleId kLogConsumerId{.Bytes = {std::byte{0xa8}}};
constexpr module::DeclarationId kLogDeclaration{.Bytes = {std::byte{0xa9}}};
struct ConsumerState
{
    bool FailStart{};
    bool FailPoll{};
    log::LogSystem* System{};
    log::SubmitResult Factory{log::SubmitResult::kDisabled};
    log::SubmitResult Destroy{log::SubmitResult::kDisabled};
    log::LogWriter Saved;
    std::function<void()> OnFactory;
    std::function<void()> OnDestroy;
};
ConsumerState*& CurrentConsumer()
{
    static ConsumerState* state{};
    return state;
}
struct Consumer
{
    ConsumerState* State;
    log::LogWriter Writer;
    Consumer(ConsumerState& state, log::LogWriter writer)
        : State(&state)
        , Writer(std::move(writer))
    {
    }
    Consumer(const Consumer&) = delete;
    Consumer& operator=(const Consumer&) = delete;
    Consumer(Consumer&&) = delete;
    Consumer& operator=(Consumer&&) = delete;
    ~Consumer()
    {
        State->Destroy = log::test::SubmitPaused(*State->System, Writer, [&](const auto& pausedWriter)
                                                 { return PG_LOG_INFO(pausedWriter, "host", "destroy"); });
        if (State->OnDestroy)
        {
            State->OnDestroy();
        }
    }
};
// ScopeHooks 固定使用可变引用。
// NOLINTNEXTLINE(misc-const-correctness)
Status StartConsumer(Consumer& consumer, FailureContext& failure) noexcept
{
    if (consumer.State->FailStart)
    {
        failure.SetPrimary(kStartupError, "startup E1");
        return std::unexpected(kStartupError);
    }
    return {};
}
// ScopeHooks 固定使用可变引用。
// NOLINTNEXTLINE(misc-const-correctness)
Status PollConsumer(Consumer& consumer, FailureContext& failure) noexcept
{
    if (consumer.State->FailPoll)
    {
        failure.SetPrimary(kCleanupError, "cleanup E2");
        return std::unexpected(kCleanupError);
    }
    return {};
}
void StopConsumer(Consumer& /*consumer*/) noexcept {}
Status RegisterConsumer(module::RegistrationContext& registration) noexcept
{
    auto* const state = CurrentConsumer();
    return registration.DeclareFactory(
        kLogDeclaration, service::Scope::kHost,
        module::ScopeFactory(
            [state](module::ScopeBuildContext& build) noexcept -> Status
            {
                state->Saved = build.Log();
                if (state->OnFactory)
                {
                    state->OnFactory();
                }
                state->Factory = log::test::SubmitPaused(*state->System, state->Saved, [&](const auto& pausedWriter)
                                                         { return PG_LOG_INFO(pausedWriter, "host", "factory"); });
                auto value = std::make_unique<Consumer>(*state, state->Saved);
                const auto result =
                    build.Own(std::move(value), module::ScopeHooks<Consumer>{.Start = StartConsumer,
                                                                             .RequestStop = StopConsumer,
                                                                             .PollStopped = PollConsumer});
                return result ? Status{} : Status{std::unexpected(result.error())};
            }));
}
const module::ModuleDefinition* QueryConsumer() noexcept
{
    static constexpr std::array kDependencies{
        module::ModuleDependency{.Id = kEngineHostModuleId, .Version = {.Major = 1}}};
    static constexpr module::ModuleDefinition kDefinition{.Descriptor = {.Id = kLogConsumerId,
                                                                         .Version = {.Major = 1},
                                                                         .Dependencies = kDependencies,
                                                                         .DisplayName = "HostLogConsumer",
                                                                         .BuildIdentity = {}},
                                                          .RegisterModule = RegisterConsumer};
    return &kDefinition;
}
template <typename Predicate>
bool Wait(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}
struct HostOwner
{
    log::detail::LogTestHooks Hooks;
    ConsumerState Consumer;
    EngineHost Host;
    HostOperationReport Report;
    HostOwner()
    {
        Consumer.System = &detail::EngineHostTestAccess::Log(Host);
        log::detail::LogTestAccess::Attach(*Consumer.System, Hooks);
    }
    ~HostOwner()
    {
        Hooks.BlockOutput.store(false);
        Hooks.BlockExit.store(false);
        Hooks.Console.CloseRetainsResource = false;
        Consumer.FailPoll = false;
        EXPECT_TRUE(Close());
    }
    HostOwner(const HostOwner&) = delete;
    HostOwner& operator=(const HostOwner&) = delete;
    HostOwner(HostOwner&&) = delete;
    HostOwner& operator=(HostOwner&&) = delete;
    Status Start(HostStartupConfig config = {})
    {
        CurrentConsumer() = &Consumer;
        const std::array queries{QueryConsumer};
        config.AdditionalResidentQueries = queries;
        const auto result = Host.Start(config, Report);
        CurrentConsumer() = nullptr;
        return result;
    }
    bool Close()
    {
        if ((Host.GetState() == HostState::kCold || Host.GetState() == HostState::kReady) &&
            !Host.BeginShutdown(Report))
        {
            return false;
        }
        return Wait([&] { return Host.TryFinishShutdown(Report).has_value(); });
    }
};
HostStartupConfig ConsoleConfig()
{
    HostStartupConfig config;
    config.Logging.Enabled = log::OutputMask::kConsole;
    config.Logging.Required = log::OutputMask::kConsole;
    return config;
}
} // namespace

TEST(EngineHostLog, OptionalInitFailureDoesNotOwnStartupCause)
{
    HostOwner owner;
    owner.Consumer.FailStart = true;
    EXPECT_FALSE(owner.Start());
    ASSERT_TRUE(owner.Report.StartupFailure);
    EXPECT_EQ(owner.Report.StartupFailure->Cause, kStartupError);
    ASSERT_TRUE(owner.Report.StartupFailure->Origin);
    EXPECT_EQ(owner.Report.StartupFailure->Origin->Message(), "startup E1");
    EXPECT_EQ(owner.Report.LogInitialization.Disposition, log::LogInitDisposition::kDegraded);
    EXPECT_TRUE(owner.Report.LogInitialization.Outputs.at(1).Failure);
    EXPECT_TRUE(owner.Close());
    EXPECT_EQ(owner.Report.StartupFailure->Cause, kStartupError);
    EXPECT_FALSE(owner.Report.LogStop->FinalFlush.OutputComplete);
}

TEST(EngineHostLog, RequiredInitFailureRollsBack)
{
    for (const int scenario : {0, 1})
    {
        SCOPED_TRACE(scenario);
        HostOwner owner;
        auto config = ConsoleConfig();
        config.Logging.Required = log::OutputMask::kConsole | log::OutputMask::kFile;
        if (scenario != 0)
        {
            config.Logging.Enabled = config.Logging.Required;
        }
        EXPECT_FALSE(owner.Start(config));
        ASSERT_TRUE(owner.Report.StartupFailure);
        EXPECT_EQ(owner.Report.StartupFailure->Stage, HostOperationStage::kLogInitialization);
        EXPECT_EQ(owner.Report.StartupFailure->Cause, log::MakeLogError(log::LogError::kInvalidConfig));
        EXPECT_FALSE(owner.Report.StartupFailure->Origin);
        EXPECT_EQ(owner.Consumer.Factory, log::SubmitResult::kDisabled);
        ASSERT_TRUE(owner.Report.LogStop);
        EXPECT_TRUE(owner.Close());
        EXPECT_EQ(owner.Host.GetState(), HostState::kClosed);
        EXPECT_TRUE(owner.Report.LogStop->ResourcesReleased);
        EXPECT_EQ(owner.Report.StartupFailure->Cause, log::MakeLogError(log::LogError::kInvalidConfig));
        EXPECT_FALSE(owner.Report.LogStop->FinalFlush.OutputComplete);
    }
}

TEST(EngineHostLog, FactoryAndDestroyUseCorrectHost)
{
    HostOwner first;
    HostOwner second;
    first.Consumer.OnFactory = [&]
    {
        EXPECT_EQ(first.Host.GetState(), HostState::kStarting);
        EXPECT_EQ(first.Host.InspectLog().State, log::LogState::kRunning);
        EXPECT_EQ(PG_LOG_DEBUG(first.Host.GetLogWriter(), "host", "starting"), log::SubmitResult::kFiltered);
    };
    first.Consumer.OnDestroy = [&]
    {
        EXPECT_EQ(first.Host.GetState(), HostState::kClosing);
        EXPECT_EQ(PG_LOG_DEBUG(first.Host.GetLogWriter(), "host", "destroying"), log::SubmitResult::kFiltered);
    };
    EXPECT_EQ(first.Host.InspectLog().State, log::LogState::kCold);
    EXPECT_EQ(PG_LOG_INFO(first.Host.GetLogWriter(), "host", "cold"), log::SubmitResult::kDisabled);
    ASSERT_TRUE(first.Start(ConsoleConfig()));
    ASSERT_TRUE(second.Start(ConsoleConfig()));
    EXPECT_EQ(first.Consumer.Factory, log::SubmitResult::kAccepted);
    EXPECT_EQ(second.Consumer.Factory, log::SubmitResult::kAccepted);
    EXPECT_TRUE(first.Close());
    EXPECT_EQ(first.Consumer.Destroy, log::SubmitResult::kAccepted);
    EXPECT_EQ(PG_LOG_INFO(first.Consumer.Saved, "host", "closed"), log::SubmitResult::kClosed);
    EXPECT_EQ(log::test::SubmitPaused(*second.Consumer.System, second.Consumer.Saved, [&](const auto& pausedWriter)
                                      { return PG_LOG_INFO(pausedWriter, "host", "second"); }),
              log::SubmitResult::kAccepted);
    EXPECT_TRUE(second.Close());
    EXPECT_EQ(first.Report.LogStop->FinalFlush.H, 2U);
    EXPECT_EQ(second.Report.LogStop->FinalFlush.H, 3U);
}

TEST(EngineHostLog, SavedWriterAndReportRemainIndependentAfterHostDestruction)
{
    log::LogWriter saved;
    HostOperationReport savedReport;
    {
        HostOwner first;
        ASSERT_TRUE(first.Start(ConsoleConfig()));
        saved = first.Consumer.Saved;
        ASSERT_TRUE(first.Close());
        savedReport = first.Report;
    }
    HostOwner second;
    ASSERT_TRUE(second.Start(ConsoleConfig()));
    auto copy = saved;
    auto tagged = copy.WithTags({});
    EXPECT_EQ(PG_LOG_INFO(tagged, "host", "destroyed host"), log::SubmitResult::kClosed);
    EXPECT_EQ(log::test::SubmitPaused(*second.Consumer.System, second.Host.GetLogWriter(), [&](const auto& pausedWriter)
                                      { return PG_LOG_INFO(pausedWriter, "host", "live host"); }),
              log::SubmitResult::kAccepted);
    if (!savedReport.LogStop)
    {
        FAIL() << "Host 析构后报告丢失";
        return;
    }
    EXPECT_TRUE(savedReport.LogStop->ResourcesReleased);
    EXPECT_EQ(savedReport.LogStop->FinalFlush.H, 2U);
    saved = {};
    copy = {};
    tagged = {};
    EXPECT_TRUE(second.Close());
    if (!second.Report.LogStop)
    {
        FAIL() << "第二个 Host 缺少最终日志报告";
        return;
    }
    EXPECT_EQ(second.Report.LogStop->FinalFlush.H, 3U);
}

TEST(EngineHostLog, ManagerDoneButLogBusyIsClosing)
{
    HostOwner owner;
    owner.Hooks.BlockExit.store(true);
    ASSERT_TRUE(owner.Start(ConsoleConfig()));
    std::optional<log::test::ConsumerPause> stoppingPause;
    // 先完成业务析构，再让首次 Log.Stop 在发布 StopRequested 前确定遇到快照锁竞争。
    // 此轮 Host 只组装缓存报告，不调用 Log.Inspect/GetWriter；取回结果后先释放屏障。
    owner.Consumer.OnDestroy = [&] { stoppingPause.emplace(*owner.Consumer.System); };
    const auto began = owner.Host.BeginShutdown(owner.Report);
    const auto before = std::chrono::steady_clock::now();
    const auto first = owner.Host.TryFinishShutdown(owner.Report);
    owner.Consumer.OnDestroy = {};
    const bool held = stoppingPause && stoppingPause->Owns();
    stoppingPause.reset();
    ASSERT_TRUE(began);
    ASSERT_TRUE(held);
    EXPECT_FALSE(first);
    EXPECT_LT(std::chrono::steady_clock::now() - before, std::chrono::milliseconds(500));
    EXPECT_EQ(owner.Host.GetState(), HostState::kClosing);
    EXPECT_EQ(owner.Report.PrimaryError, log::MakeLogError(log::LogError::kBusy));
    EXPECT_TRUE(owner.Report.WaitingForLog);
    ASSERT_TRUE(owner.Report.LogStop);
    EXPECT_FALSE(owner.Report.LogStop->ResourcesReleased);
    EXPECT_EQ(owner.Host.InspectLog().State, log::LogState::kRunning);
    EXPECT_FALSE(owner.Hooks.ExitEntered.load());
    ASSERT_TRUE(Wait(
        [&]
        {
            const auto progressed = owner.Host.TryFinishShutdown(owner.Report);
            EXPECT_FALSE(progressed);
            EXPECT_EQ(owner.Host.GetState(), HostState::kClosing);
            EXPECT_EQ(owner.Report.PrimaryError, log::MakeLogError(log::LogError::kBusy));
            EXPECT_TRUE(owner.Report.WaitingForLog);
            EXPECT_TRUE(owner.Report.LogStop);
            if (owner.Report.LogStop)
            {
                EXPECT_FALSE(owner.Report.LogStop->ResourcesReleased);
            }
            return owner.Hooks.ExitEntered.load();
        }));
    EXPECT_TRUE(owner.Host.InspectModules()->empty());
    EXPECT_EQ(owner.Hooks.WorkerHandles.load(), 1U);
    owner.Hooks.BlockExit.store(false);
    EXPECT_TRUE(owner.Close());
    EXPECT_TRUE(owner.Report.LogStop->ResourcesReleased);
}

TEST(EngineHostLog, FailedStartRetainsCauseWhileLogDrains)
{
    HostOwner owner;
    owner.Hooks.BlockOutput.store(true);
    owner.Hooks.Console.FlushFailure = log::MakeLogError(log::LogError::kIoFailed);
    owner.Consumer.FailStart = true;
    owner.Consumer.FailPoll = true;
    EXPECT_FALSE(owner.Start(ConsoleConfig()));
    EXPECT_EQ(owner.Report.PrimaryError, kStartupError);
    ASSERT_TRUE(owner.Report.StartupFailure);
    EXPECT_EQ(owner.Report.StartupFailure->Cause, kStartupError);
    ASSERT_TRUE(owner.Report.ModuleReport);
    EXPECT_EQ(owner.Report.ModuleReport->CleanupError, kCleanupError);
    {
        const auto copied = owner.Report;
        owner.Report = {};
        ASSERT_TRUE(copied.ModuleReport);
        EXPECT_EQ(copied.Failure(), &copied.ModuleReport->Failure);
        EXPECT_EQ(copied.StartupFailure->Origin->Message(), "startup E1");
        EXPECT_EQ(copied.StartupFailure->Cause, kStartupError);
        EXPECT_EQ(copied.ModuleReport->CleanupError, kCleanupError);
        owner.Report = copied;
        EXPECT_NE(owner.Report.Failure(), copied.Failure());
    }
    EXPECT_EQ(owner.Host.GetState(), HostState::kClosing);
    EXPECT_EQ(PG_LOG_INFO(owner.Consumer.Saved, "host", "cleanup still running"), log::SubmitResult::kAccepted);
    owner.Consumer.FailPoll = false;
    EXPECT_FALSE(owner.Host.TryFinishShutdown(owner.Report));
    EXPECT_EQ(owner.Report.StartupFailure->Origin->Message(), "startup E1");
    EXPECT_EQ(owner.Report.PrimaryError, log::MakeLogError(log::LogError::kBusy));
    ASSERT_TRUE(Wait([&] { return owner.Hooks.OutputEntered.load(); }));
    owner.Hooks.BlockOutput.store(false);
    EXPECT_TRUE(owner.Close());
    EXPECT_EQ(owner.Report.StartupFailure->Cause, kStartupError);
    EXPECT_EQ(owner.Report.LogStop->FinalFlush.Outputs.at(0).Failure, log::MakeLogError(log::LogError::kIoFailed));
    EXPECT_FALSE(owner.Report.LogStop->FinalFlush.OutputComplete);
}

TEST(EngineHostLog, ClosedCanHaveIncompleteLog)
{
    HostOwner owner;
    owner.Hooks.Console.WriteFailure = log::MakeLogError(log::LogError::kIoFailed);
    ASSERT_TRUE(owner.Start(ConsoleConfig()));
    EXPECT_TRUE(owner.Close());
    EXPECT_EQ(owner.Host.GetState(), HostState::kClosed);
    EXPECT_FALSE(owner.Report.PrimaryError);
    ASSERT_TRUE(owner.Report.LogStop);
    EXPECT_TRUE(owner.Report.LogStop->ResourcesReleased);
    EXPECT_FALSE(owner.Report.LogStop->FinalFlush.OutputComplete);
    EXPECT_GT(owner.Report.LogStop->FinalFlush.Outputs.at(0).Undelivered, 0U);
    EXPECT_LE(sizeof(HostOperationReport), 16384U);
    std::cout << "HostReport=" << sizeof(HostOperationReport) << " Startup=" << sizeof(HostStartupFailure)
              << " StartupCache=" << sizeof(std::optional<HostStartupFailure>)
              << " ModuleSummary=" << sizeof(HostStartupModuleSummary)
              << " LogInitSummary=" << sizeof(HostLogInitialization)
              << " FileSnapshot=" << sizeof(log::FileStoreSnapshot) << '\n';
    // 嵌套 Module 峰值含唯一复用的 Module out；此阶段完整 Host 仅调用者一份。
    // Log 两份 Snapshot、最终 Stop、FileStore 状态和旧 Flush 均计入共享交集。
    constexpr std::size_t kLogPersistentReports =
        (2 * sizeof(log::LogSnapshot)) + sizeof(log::StopReport) + sizeof(log::FileStoreSnapshot) +
        sizeof(std::optional<log::FlushReport>) + (2 * alignof(std::optional<log::FlushReport>));
    constexpr std::size_t kSharedPeak = 48016 + sizeof(HostOperationReport) +
                                        sizeof(std::optional<HostStartupFailure>) + sizeof(HostLogInitialization) +
                                        kLogPersistentReports + 512 + 384;
    static_assert(kSharedPeak <= 65536);
    std::cout << "SharedModulePeak=" << kSharedPeak << " LogPersistentReports=" << kLogPersistentReports << '\n';
    static_assert(sizeof(log::detail::QueueSnapshot) + sizeof(std::optional<log::detail::QueueSnapshot>) <= 384);
    static_assert(sizeof(std::optional<log::detail::QueueSnapshot>) + (2 * sizeof(log::detail::WriterSnapshot)) <= 384);
    std::cout << "QueueSnapshot=" << sizeof(log::detail::QueueSnapshot)
              << " OptionalQueue=" << sizeof(std::optional<log::detail::QueueSnapshot>)
              << " WriterSnapshot=" << sizeof(log::detail::WriterSnapshot) << '\n';
}

TEST(EngineHostLog, TwoHostsHaveIndependentPartitions)
{
    const auto directory = std::filesystem::current_path() / "HostLogPartitions";
    const auto firstDirectory = directory / "first";
    const auto secondDirectory = directory / "second";
    std::error_code error;
    std::filesystem::create_directories(firstDirectory, error);
    ASSERT_FALSE(error);
    std::filesystem::create_directories(secondDirectory, error);
    ASSERT_FALSE(error);
    HostOwner first;
    HostOwner second;
    auto config = ConsoleConfig();
    config.Logging.Enabled = log::OutputMask::kFile;
    config.Logging.Required = log::OutputMask::kFile;
    config.Logging.PartitionId.front() = std::byte{0x71};
    config.Logging.Directory = firstDirectory.native();
    ASSERT_TRUE(first.Start(config));
    config.Logging.PartitionId.front() = std::byte{0x72};
    config.Logging.Directory = secondDirectory.native();
    ASSERT_TRUE(second.Start(config));
    const auto firstSnapshot = first.Host.InspectLog();
    const auto secondSnapshot = second.Host.InspectLog();
    EXPECT_NE(firstSnapshot.File.ActivePath.View(), secondSnapshot.File.ActivePath.View());
    EXPECT_TRUE(firstSnapshot.File.LeaseHeld);
    EXPECT_TRUE(secondSnapshot.File.LeaseHeld);
    EXPECT_TRUE(first.Close());
    EXPECT_EQ(second.Host.InspectLog().State, log::LogState::kRunning);
    EXPECT_TRUE(second.Close());
    EXPECT_TRUE(first.Report.LogStop->FinalFlush.OutputComplete);
    EXPECT_TRUE(second.Report.LogStop->FinalFlush.OutputComplete);
    EXPECT_EQ(first.Hooks.WorkerHandles.load(), 0U);
    EXPECT_EQ(second.Hooks.DataBlocks.load(), 0U);
    for (const auto* snapshot : {&firstSnapshot, &secondSnapshot})
    {
        std::ifstream file(std::filesystem::path(snapshot->File.ActivePath.View()), std::ios::binary);
        ASSERT_TRUE(file);
        const std::string contents{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
        EXPECT_NE(contents.find("message=factory"), std::string::npos);
        EXPECT_NE(contents.find("message=destroy"), std::string::npos);
    }
}
} // namespace pg::engine::test
