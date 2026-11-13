#include "../CMake/SourcePluginProject/MyGame/Source/LogOutcome.h"
#include "ConsumerPause.h"
#include <FailureContext.h>
#include <FailureReport.h>
#include <Log.h>
#include <LogConfig.h>
#include <LogReports.h>
#include <LogSystem.h>
#include <LogTestAccess.h>
#include <LogTypes.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#ifdef _WIN32
#include <malloc.h> // NOLINT(misc-include-cleaner) MSVC 原生对齐分配入口。
#endif

namespace
{
// 仅独立测试 EXE 的探针；Windows 不拦截 DLL/CRT 内部的独立分配入口。
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> gArmed;
std::atomic<unsigned> gRequests;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
void RejectAllocation() noexcept
{
    if (gArmed.load())
    {
        ++gRequests;
        std::_Exit(91);
    }
}
// 该进程成对替换 new/delete，以原生分配实现探针且不递归进入自身。
// NOLINTBEGIN(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
void* Allocate(std::size_t size, std::size_t alignment = 0)
{
    RejectAllocation();
    void* memory{};
    if (alignment == 0)
    {
        memory = std::malloc(size == 0 ? 1 : size);
    }
    else
    {
#ifdef _WIN32
        // NOLINTNEXTLINE(misc-include-cleaner) MSVC 的 malloc.h 声明该入口。
        memory = _aligned_malloc(size == 0 ? 1 : size, alignment);
#else
        if (posix_memalign(&memory, alignment, size == 0 ? 1 : size) != 0)
        {
            memory = nullptr;
        }
#endif
    }
    if (memory == nullptr)
    {
        std::_Exit(92);
    }
    return memory;
}
void FreeAligned(void* memory) noexcept
{
#ifdef _WIN32
    // NOLINTNEXTLINE(misc-include-cleaner) MSVC 的 malloc.h 声明该入口。
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}
// NOLINTEND(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
bool RunFailures(const pg::log::LogConfig& config)
{
    using namespace pg::log;
    constexpr std::array kStages{
        detail::LogInitStage::kConfigCopy,
        detail::LogInitStage::kDataAllocation,
        detail::LogInitStage::kQueue,
        detail::LogInitStage::kControlAllocation,
        detail::LogInitStage::kSynchronization,
        detail::LogInitStage::kConsole,
        detail::LogInitStage::kFile,
        detail::LogInitStage::kBackend,
        detail::LogInitStage::kThread,
    };
    for (const auto stage : kStages)
    {
        detail::LogTestHooks hooks;
        hooks.FailAt = stage;
        LogSystem system;
        detail::LogTestAccess::Attach(system, hooks);
        LogInitReport initialized;
        const auto result = system.Initialize(config, initialized);
        StopReport stopped;
        const auto closed = system.Stop(std::chrono::steady_clock::now() + std::chrono::seconds(2), stopped);
        if (result || !initialized.Failure || result.error() != *initialized.Failure || initialized.CleanupPending ||
            !closed || !stopped.ResourcesReleased || system.GetState() != LogState::kClosed || hooks.DataBlocks != 0 ||
            hooks.ControlOwners != 0 || hooks.WorkerHandles != 0 ||
            PG_LOG_INFO(system.GetWriter(), "runtime", "failed-init") != SubmitResult::kDisabled)
        {
            return false;
        }
    }
    return true;
}
} // namespace

// NOLINTBEGIN(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
void* operator new(std::size_t size) { return Allocate(size); }
void* operator new[](std::size_t size) { return Allocate(size); }
// NOLINTNEXTLINE(clang-analyzer-unix.MismatchedDeallocator) 与本 EXE 的 malloc 配对。
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void* operator new(std::size_t size, const std::nothrow_t& /*tag*/) noexcept { return Allocate(size); }
void* operator new[](std::size_t size, const std::nothrow_t& /*tag*/) noexcept { return Allocate(size); }
void operator delete(void* memory, const std::nothrow_t& /*tag*/) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::nothrow_t& /*tag*/) noexcept { std::free(memory); }
void* operator new(std::size_t size, std::align_val_t alignment)
{
    return Allocate(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return Allocate(size, static_cast<std::size_t>(alignment));
}
void operator delete(void* memory, std::align_val_t /*alignment*/) noexcept { FreeAligned(memory); }
void operator delete[](void* memory, std::align_val_t /*alignment*/) noexcept { FreeAligned(memory); }
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t& /*tag*/) noexcept
{
    return Allocate(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t& /*tag*/) noexcept
{
    return Allocate(size, static_cast<std::size_t>(alignment));
}
void operator delete(void* memory, std::align_val_t /*alignment*/, const std::nothrow_t& /*tag*/) noexcept
{
    FreeAligned(memory);
}
void operator delete[](void* memory, std::align_val_t /*alignment*/, const std::nothrow_t& /*tag*/) noexcept
{
    FreeAligned(memory);
}
// NOLINTEND(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)

int main(int argc, char* argv[])
{
    bool forceDrop{};
    if (argc == 2)
    {
        const std::span arguments(argv, static_cast<std::size_t>(argc));
        const std::string_view mode(arguments.subspan(1).front());
        forceDrop = mode == "first-drop";
        if (!forceDrop)
        {
            gArmed.store(true);
            if (mode == "throwing-new")
            {
                ::operator delete(::operator new(32));
            }
            else if (mode == "nothrow-new")
            {
                ::operator delete(::operator new(32, std::nothrow), std::nothrow);
            }
            else if (mode == "aligned-new")
            {
                ::operator delete(::operator new(64, std::align_val_t{64}), std::align_val_t{64});
            }
            return 2;
        }
    }
    using namespace pg::log;
    const std::filesystem::path directory(PG_LOG_RUNTIME_ROOT);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        return 3;
    }
    const auto& nativeDirectory = directory.native();
    LogConfig config;
    config.Directory = nativeDirectory;
    config.PartitionId.front() = std::byte{0x12};
    config.Required = config.Enabled;
    if (!RunFailures(config))
    {
        return 4;
    }
    LogSystem system;
    LogInitReport initialized;
    if (!system.Initialize(config, initialized))
    {
        return 5;
    }
    const std::filesystem::path logPath(initialized.FilePath.View());
    StopReport observed;
    SubmitResult observedSubmit{SubmitResult::kDisabled};
    gArmed.store(true);
    bool success{};
    {
        LogSystem disabled;
        LogConfig disabledConfig;
        disabledConfig.Enabled = OutputMask::kNone;
        LogInitReport disabledReport;
        const auto disabledStarted = disabled.Initialize(disabledConfig, disabledReport);
        const auto writer = system.GetWriter();
        auto copy = writer.WithTags({});
        SubmitResult submitted{SubmitResult::kDisabled};
        {
            const test::ConsumerPause pause(system);
            if (pause.Owns())
            {
                if (forceDrop)
                {
                    detail::LogTestAccess::HoldQueue(system);
                }
                submitted = PG_LOG_INFO(copy, "runtime", "{} {} {} {}", 42, 1.25, true, "bounded");
                if (forceDrop)
                {
                    detail::LogTestAccess::ReleaseQueue(system);
                }
            }
        }
        const auto snapshot = system.Inspect();
        pg::FailureReport failure;
        pg::FailureContext(failure).SetPrimary(MakeLogError(LogError::kIoFailed), "runtime failure");
        const auto copied = failure;
        const auto site = copied.PrimarySite();
        failure.Clear();
        FlushReport flushed;
        const auto flush = system.Flush(std::chrono::steady_clock::now() + std::chrono::seconds(2), flushed);
        StopReport stopped;
        const auto stop = system.Stop(std::chrono::steady_clock::now() + std::chrono::seconds(2), stopped);
        observed = stopped;
        observedSubmit = submitted;
        // 共用应用判据，但伪造完整性、范围、单路送达和释放状态都不能通过。
        auto invalid = stopped;
        invalid.FinalFlush.OutputComplete = forceDrop;
        const bool rejectsWrongComplete = !pg::fixture::ValidateLogOutcome(submitted, invalid);
        invalid = stopped;
        invalid.FinalFlush.H = forceDrop ? 1U : 0U;
        const bool rejectsWrongH = !pg::fixture::ValidateLogOutcome(submitted, invalid);
        invalid = stopped;
        invalid.FinalFlush.Outputs.at(1).Delivered = forceDrop ? 1U : 0U;
        const bool rejectsWrongDelivery = !pg::fixture::ValidateLogOutcome(submitted, invalid);
        invalid = stopped;
        invalid.ResourcesReleased = false;
        const bool rejectsUnreleased = !pg::fixture::ValidateLogOutcome(submitted, invalid);
        success = disabledStarted && submitted == (forceDrop ? SubmitResult::kDropped : SubmitResult::kAccepted) &&
                  snapshot.HighestAccepted == (forceDrop ? 0U : 1U) && site && site->Message() == "runtime failure" &&
                  flush && flushed.Finished && flushed.OutputComplete == !forceDrop &&
                  flushed.H == (forceDrop ? 0U : 1U) && stop && pg::fixture::ValidateLogOutcome(submitted, stopped) &&
                  rejectsWrongComplete && rejectsWrongH && rejectsWrongDelivery && rejectsUnreleased &&
                  !pg::fixture::ValidateLogOutcome(SubmitResult::kClosed, stopped) &&
                  !pg::fixture::ValidateLogOutcome(SubmitResult::kDisabled, stopped) &&
                  !pg::fixture::ValidateLogOutcome(SubmitResult::kFiltered, stopped) &&
                  PG_LOG_INFO(copy, "runtime", "closed") == SubmitResult::kClosed;
    }
    gArmed.store(false);
    if (!forceDrop)
    {
        std::ifstream file(logPath);
        const std::string text(std::istreambuf_iterator<char>{file}, {});
        success = success && file && text.contains("42 1.25 true bounded");
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,modernize-use-std-print)
    std::printf("RuntimeAllocationRequests=%u FailureStages=9 PublicGate=1 FirstDrop=%u\n", gRequests.load(),
                forceDrop ? 1U : 0U);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,modernize-use-std-print)
    std::printf("RuntimeOutcome H=%llu accepted=%u dropped=%llu console=%llu file=%llu resources=%u complete=%u\n",
                static_cast<unsigned long long>(observed.FinalFlush.H),
                observedSubmit == SubmitResult::kAccepted ? 1U : 0U,
                static_cast<unsigned long long>(observed.FinalFlush.Rejected.at(2).Contention),
                static_cast<unsigned long long>(observed.FinalFlush.Outputs.at(0).Delivered),
                static_cast<unsigned long long>(observed.FinalFlush.Outputs.at(1).Delivered),
                observed.ResourcesReleased ? 1U : 0U, observed.FinalFlush.OutputComplete ? 1U : 0U);
    return success && gRequests.load() == 0 ? 0 : 6;
}
