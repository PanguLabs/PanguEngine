#include "ConsumerPause.h"
#include "Fixtures/LogProducer.h"
#include <EngineHost.h>
#include <EngineHostTestAccess.h>
#include <FailureReport.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <Log.h>
#include <LogTestAccess.h>
#include <LogTypes.h>
#include <LogWriter.h>
#include <ModuleManager.h>
#include <gtest/gtest.h>
#ifdef PG_TEST_MODULAR
#include "LogProducerPath.h"
#endif

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <thread>

namespace
{
using namespace pg;
using namespace pg::log;
template <class Predicate>
bool Wait(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;)
    {
        if (predicate())
        {
            return true;
        }
        std::this_thread::yield();
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
    }
}
struct HostOwner
{
    log::detail::LogTestHooks Hooks;
    engine::EngineHost Host;
    engine::HostOperationReport Report;
    HostOwner() { log::detail::LogTestAccess::Attach(engine::detail::EngineHostTestAccess::Log(Host), Hooks); }
    HostOwner(const HostOwner&) = delete;
    HostOwner& operator=(const HostOwner&) = delete;
    HostOwner(HostOwner&&) = delete;
    HostOwner& operator=(HostOwner&&) = delete;
    ~HostOwner()
    {
        Hooks.BlockSequence.store(0);
        EXPECT_TRUE(Close());
    }
    bool Close()
    {
        if (Host.GetState() == engine::HostState::kReady || Host.GetState() == engine::HostState::kCold)
        {
            if (!Host.BeginShutdown(Report))
            {
                return false;
            }
        }
        return Wait([&] { return Host.TryFinishShutdown(Report).has_value(); });
    }
};
auto Candidates()
{
#ifdef PG_TEST_MODULAR
    return std::array{
        module::ModuleLoadCandidate{
            module::SharedModuleCandidate{.ExpectedId = test::kProducerId, .Path = test::kProducerPath}},
    };
#else
    return std::array{module::ModuleLoadCandidate{module::StaticModuleCandidate{test::QueryLogProducer}}};
#endif
}
void RunLifetime(bool inspectAfterHost)
{
    const std::filesystem::path marker(PG_LOG_NATIVE_MARKER);
    std::error_code error;
    std::filesystem::remove(marker, error);
    ASSERT_FALSE(error);
    const auto directory = marker.parent_path() / (inspectAfterHost ? "AfterHost" : "OwnedRecord");
    std::filesystem::create_directories(directory, error);
    ASSERT_FALSE(error);
    LogWriter saved;
    FailureReport failure;
    engine::HostOperationReport savedReport;
    std::filesystem::path logPath;
    {
        HostOwner owner;
        owner.Hooks.BlockSequence.store(1);
        const auto candidates = Candidates();
        const auto& nativeDirectory = directory.native();
        engine::HostStartupConfig config;
        config.Modules = candidates;
        config.Logging.Enabled = OutputMask::kFile;
        config.Logging.Required = OutputMask::kFile;
        config.Logging.Directory = nativeDirectory;
        config.Logging.PartitionId.front() = std::byte{0x11};
        config.Logging.DefaultWait = std::chrono::milliseconds(0);
        ASSERT_TRUE(owner.Host.Start(config, owner.Report));
        logPath = std::filesystem::path(owner.Host.InspectLog().File.ActivePath.View());
        auto access = test::AcquireProducer(owner.Host, owner.Host.HostScope());
        ASSERT_TRUE(access);
        auto borrow = access->TryBorrow();
        ASSERT_TRUE(borrow);
        saved = (*borrow)->Writer;
        {
            const test::ConsumerPause pause(engine::detail::EngineHostTestAccess::Log(owner.Host));
            ASSERT_TRUE(pause.Owns());
            ASSERT_EQ((*borrow)->Produce(saved, failure, test::ProducerPhase::kStack), SubmitResult::kAccepted);
        }
        ASSERT_TRUE(Wait([&] { return owner.Hooks.SequenceEntered.load(); }));
        // 屏障已越过首次 TryPop，静态记录不会与该次消费竞争接纳锁。
        ASSERT_EQ((*borrow)->Produce(saved, failure, test::ProducerPhase::kStatic), SubmitResult::kAccepted);
        EXPECT_EQ(owner.Host.InspectLog().HighestProcessed, 0U);
        borrow->Release();
        *access = {};
        if (!inspectAfterHost)
        {
            saved = {};
        }
        ASSERT_TRUE(owner.Host.BeginShutdown(owner.Report));
#ifdef PG_TEST_MODULAR
        const bool physicalUnloadObserved = Wait(
            [&]
            {
                const auto progress = owner.Host.TryFinishShutdown(owner.Report);
                return !progress && owner.Report.WaitingForLog && std::filesystem::exists(marker);
            });
        ASSERT_TRUE(physicalUnloadObserved);
        EXPECT_EQ(owner.Host.GetState(), engine::HostState::kClosing);
        EXPECT_TRUE(owner.Report.WaitingForLog);
        EXPECT_EQ(owner.Host.InspectLog().HighestProcessed, 0U);
#else
        EXPECT_FALSE(owner.Host.TryFinishShutdown(owner.Report));
        EXPECT_FALSE(std::filesystem::exists(marker));
#endif
        // 原生卸载发生后才允许首次编码、输出和统计记录。
        owner.Hooks.BlockSequence.store(0);
        ASSERT_TRUE(owner.Close());
        if (!owner.Report.LogStop)
        {
            FAIL() << "缺少最终日志报告";
            return;
        }
        EXPECT_TRUE(owner.Report.LogStop->ResourcesReleased);
        EXPECT_TRUE(owner.Report.LogStop->FinalFlush.OutputComplete);
        EXPECT_EQ(owner.Report.LogStop->FinalFlush.H, 2U);
        savedReport = owner.Report;
    }
    // Host 对象及业务库均已结束；以下操作不得读取旧 Data 或 Plugin 代码。
    if (inspectAfterHost)
    {
        auto copy = saved;
        LogTags tags;
        tags.Module = test::kProducerId;
        auto tagged = copy.WithTags(tags);
        const auto afterHostResult = PG_LOG_INFO(tagged, "after-host", "closed {}", 7);
        EXPECT_EQ(afterHostResult, SubmitResult::kClosed);
        saved = {};
        copy = {};
        tagged = {};
    }
    if (!savedReport.LogStop)
    {
        FAIL() << "Host 析构后报告丢失";
        return;
    }
    EXPECT_TRUE(savedReport.LogStop->ResourcesReleased);
    const auto copiedFailure = failure;
    failure.Clear();
    const auto site = copiedFailure.PrimarySite();
    if (!site)
    {
        FAIL() << "Plugin 失败现场丢失";
        return;
    }
    EXPECT_EQ(site->Message(), "plugin stack failure");
    EXPECT_TRUE(site->OriginKnown());
    EXPECT_NE(site->File().find("LogProducer.cpp"), std::string_view::npos);
    EXPECT_NE(site->Function().find("Produce"), std::string_view::npos);
    std::ifstream input(logPath);
    ASSERT_TRUE(input);
    const std::string savedText(std::istreambuf_iterator<char>{input}, {});
    EXPECT_NE(savedText.find("plugin stack message 42"), std::string::npos);
    EXPECT_NE(savedText.find("plugin static message"), std::string::npos);
    EXPECT_NE(savedText.find("plugin-stack-category"), std::string::npos);
    EXPECT_NE(savedText.find("plugin-static-category"), std::string::npos);
    EXPECT_NE(savedText.find("LogProducer.cpp"), std::string::npos);
    EXPECT_NE(savedText.find("Produce"), std::string::npos);
}
} // namespace

TEST(LogLifetime, OwnedRecordSurvivesPhysicalUnload) { RunLifetime(false); }
TEST(LogLifetime, WriterOutlivesHostAndDoesNotPinProducer) { RunLifetime(true); }
