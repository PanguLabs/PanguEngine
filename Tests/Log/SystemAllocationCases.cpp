#include "SystemAllocationCases.h"
#include "ConsumerPause.h"
#include "LogConfig.h"
#include "LogReports.h"
#include "LogTypes.h"
#include <Log.h>
#include <LogSystem.h>
#include <LogTestAccess.h>
#include <Result.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace
{
// 监督进程在关卡前初始化，关卡内只执行真实提交、消费者、查询和停止。
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
pg::log::LogSystem gSystem;
#if defined(_WIN32) && defined(_DEBUG)
unsigned int gAllocationNumber{};
unsigned int gFailAllocationNumber{};
unsigned int gRejectedAllocations{};
#endif
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
#if defined(_WIN32) && defined(_DEBUG)
int RejectDebugAllocation(int operation, void* /*memory*/, std::size_t /*size*/, int kind, long /*request*/,
                          const unsigned char* /*file*/, int /*line*/) noexcept
{
    // 仅初始化线程的普通 malloc；CRT 自身内部块不参与两个显式 Try 分配的序号。
    if (operation == _HOOK_ALLOC && kind == _NORMAL_BLOCK && ++gAllocationNumber == gFailAllocationNumber)
    {
        ++gRejectedAllocations;
        return 0;
    }
    return 1;
}
class DebugAllocationFailure final
{
public:
    explicit DebugAllocationFailure(unsigned int number) noexcept
        : Previous(_CrtSetAllocHook(RejectDebugAllocation))
    {
        gAllocationNumber = 0;
        gFailAllocationNumber = number;
    }
    ~DebugAllocationFailure() { static_cast<void>(_CrtSetAllocHook(Previous)); }
    DebugAllocationFailure(const DebugAllocationFailure&) = delete;
    DebugAllocationFailure& operator=(const DebugAllocationFailure&) = delete;
    DebugAllocationFailure(DebugAllocationFailure&&) = delete;
    DebugAllocationFailure& operator=(DebugAllocationFailure&&) = delete;

private:
    _CRT_ALLOC_HOOK Previous;
};
#endif
} // namespace
bool RunDebugSystemOutOfMemoryCases() noexcept
{
#if defined(_WIN32) && defined(_DEBUG)
    void* memory{};
    {
        const DebugAllocationFailure failure{1};
        // 负对照必须调用真实 CRT，防止替换、内联或错误 Runtime 使关卡空测。
        auto* (*volatile allocate)(std::size_t) = std::malloc;
        memory = allocate(1);
    }
    const bool probeWorked = memory == nullptr && gRejectedAllocations == 1;
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    std::free(memory);
    if (!probeWorked)
    {
        return false;
    }
    for (const auto number : {1U, 2U})
    {
        pg::log::LogSystem system;
        pg::log::detail::LogTestHooks hooks;
        pg::log::detail::LogTestAccess::Attach(system, hooks);
        pg::log::LogConfig config;
        config.Enabled = pg::log::OutputMask::kConsole;
        pg::log::LogInitReport report;
        pg::Status initialized;
        {
            const DebugAllocationFailure failure{number};
            initialized = system.Initialize(config, report);
        }
        // Hook 已恢复，结构化结果和清理责任的检查不在分配故障域内。
        pg::log::StopReport stopped;
        if (initialized || initialized.error() != pg::log::MakeLogError(pg::log::LogError::kOutOfMemory) ||
            report.CleanupPending || system.GetState() != pg::log::LogState::kClosed ||
            !system.Stop(std::chrono::steady_clock::now(), stopped) || !stopped.ResourcesReleased ||
            hooks.DataBlocks.load() != 0 || hooks.ControlOwners.load() != 0 || hooks.WorkerHandles.load() != 0 ||
            gRejectedAllocations != number + 1)
        {
            return false;
        }
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,modernize-use-std-print)
    std::printf("DebugInitializationOomCases=2 ProbeRejected=1 InitializationRejected=2\n");
#endif
    return true;
}
bool PrepareSystemAllocationCases() noexcept
{
    pg::log::LogConfig config;
    config.Enabled = pg::log::OutputMask::kConsole;
    pg::log::LogInitReport report;
    return gSystem.Initialize(config, report).has_value();
}
bool RunSystemOutOfMemoryCase() noexcept
{
    pg::log::LogSystem system;
    pg::log::LogConfig config;
    config.Enabled = pg::log::OutputMask::kConsole;
    pg::log::LogInitReport report;
    const auto initialized = system.Initialize(config, report);
    pg::log::StopReport stopped;
    return !initialized && initialized.error() == pg::log::MakeLogError(pg::log::LogError::kOutOfMemory) &&
           !report.CleanupPending && system.GetState() == pg::log::LogState::kClosed &&
           system.Stop(std::chrono::steady_clock::now(), stopped).has_value() && stopped.ResourcesReleased;
}
bool RunSystemInitializationAllocationCase() noexcept
{
    pg::log::LogSystem system;
    pg::log::detail::LogTestHooks hooks;
    pg::log::detail::LogTestAccess::Attach(system, hooks);
    pg::log::LogConfig config;
    config.Enabled = pg::log::OutputMask::kConsole;
    pg::log::LogInitReport report;
    const auto initialized = system.Initialize(config, report);
    const bool running = initialized && report.Ready == config.Enabled &&
                         system.GetState() == pg::log::LogState::kRunning && hooks.DataBlocks.load() == 1 &&
                         hooks.ControlOwners.load() == 1 && hooks.WorkerHandles.load() == 1;
    pg::log::StopReport stopped;
    const auto result = system.Stop(std::chrono::steady_clock::now() + std::chrono::seconds(2), stopped);
    return running && result && stopped.ResourcesReleased && hooks.DataBlocks.load() == 0 &&
           hooks.ControlOwners.load() == 0 && hooks.WorkerHandles.load() == 0;
}
bool RunSystemAllocationCases() noexcept
{
    pg::log::LogSystem disabled;
    pg::log::LogConfig config;
    config.Enabled = pg::log::OutputMask::kNone;
    pg::log::LogInitReport report;
    if (!disabled.Initialize(config, report))
    {
        return false;
    }
    const auto writer = gSystem.GetWriter();
    const auto submitted =
        pg::log::test::SubmitPaused(gSystem, writer,
                                    [&](const auto& pausedWriter)
                                    {
                                        return PG_LOG_ERROR(pausedWriter, "allocation", "{} {} {} {}", 42, 1.25,
                                                            pg::log::kLogErrorDomain.Value(), "bounded");
                                    });
    const auto snapshot = gSystem.Inspect();
    pg::log::FlushReport flushed;
    if (!gSystem.Flush(std::chrono::steady_clock::now() + std::chrono::seconds(2), flushed) || !flushed.Finished ||
        !flushed.OutputComplete || flushed.H != 1)
    {
        return false;
    }
    pg::log::StopReport stopped;
    const auto result = gSystem.Stop(std::chrono::steady_clock::now() + std::chrono::seconds(2), stopped);
    const auto usage = pg::log::detail::LogTestAccess::MemoryUsage(config);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,modernize-use-std-print)
    std::printf("LogSystemBytes=%zu DataBytes=%zu ControlBytes=%zu SubmissionWorkBytes=%zu ManagementWorkBytes=%zu "
                "StackBytes=%zu RequiredBytes=%zu\n",
                usage.ObjectBytes, usage.DataBytes, usage.ControlBytes, usage.SubmissionWorkBytes,
                usage.ManagementWorkBytes, usage.WorkerStackBytes, usage.RequiredBytes);
    return submitted == pg::log::SubmitResult::kAccepted && snapshot.HighestAccepted == 1 && result &&
           stopped.ResourcesReleased && stopped.FinalFlush.OutputComplete && stopped.FinalFlush.H == 1;
}
