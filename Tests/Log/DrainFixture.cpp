#include "ConsumerPause.h"
#include "DrainTestSupport.h"

#include <Log.h>
#include <LogConfig.h>
#include <LogReports.h>
#include <LogTestAccess.h>
#include <LogTypes.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <thread>

// main 保持标准 C ABI 签名。
// NOLINTNEXTLINE(misc-const-correctness)
int main(int argc, char** argv)
{
    if (argc != 2)
    {
        return 1;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const std::string_view mode{argv[1]};
    if (mode == "memory")
    {
        const pg::log::LogConfig config;
        const auto usage = pg::log::detail::LogTestAccess::MemoryUsage(config);
        // 预算探针只使用 C stdio，避免为打印结果引入 fmt 或额外分配路径。
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,modernize-use-std-print)
        std::printf("Object=%zu Data=%zu Control=%zu Submission=%zu Stack=%zu Management=%zu Required=%zu "
                    "Init=%zu Snapshot=%zu Flush=%zu Stop=%zu\n",
                    usage.ObjectBytes, usage.DataBytes, usage.ControlBytes, usage.SubmissionWorkBytes,
                    usage.WorkerStackBytes, usage.ManagementWorkBytes, usage.RequiredBytes,
                    sizeof(pg::log::LogInitReport), sizeof(pg::log::LogSnapshot), sizeof(pg::log::FlushReport),
                    sizeof(pg::log::StopReport));
        return usage.RequiredBytes <= config.MemoryBudgetBytes ? 0 : 7;
    }
    if (mode == "control-exit42" || mode == "control-crash" || mode == "control-hang")
    {
        std::puts("FATAL_READY");
        std::fflush(stdout);
        if (mode == "control-exit42")
        {
            std::_Exit(42);
        }
        if (mode == "control-crash")
        {
            __builtin_trap();
        }
        for (;;)
        {
            std::this_thread::yield();
        }
    }
    pg::log::test::DrainOwner owner;
    if (!owner.Start())
    {
        return 2;
    }
    if (mode == "queue-deadline")
    {
        pg::log::detail::LogTestAccess::HoldQueue(owner.System);
        pg::log::FlushReport pending;
        const auto before = std::chrono::steady_clock::now();
        const auto flushed = owner.System.Flush(before + std::chrono::milliseconds(10), pending);
        const bool bounded = !flushed && std::chrono::steady_clock::now() - before < std::chrono::milliseconds(500);
        pg::log::detail::LogTestAccess::ReleaseQueue(owner.System);
        return bounded && owner.System.Flush(pg::log::test::Deadline(), pending) && pending.Finished ? 0 : 8;
    }
    owner.Hooks.BlockOutput.store(true);
    if (pg::log::test::SubmitPaused(owner.System, owner.System.GetWriter(), [&](const auto& pausedWriter)
                                    { return PG_LOG_INFO(pausedWriter, "drain", "blocked"); }) !=
            pg::log::SubmitResult::kAccepted ||
        !pg::log::test::Wait([&] { return owner.Hooks.OutputEntered.load(); }))
    {
        std::_Exit(3);
    }
    pg::log::FlushReport prefix;
    const auto flush = pg::log::test::Flush(owner.System, std::chrono::steady_clock::now(), prefix);
    if (flush || prefix.H != 1)
    {
        owner.Hooks.BlockOutput.store(false);
        return 4;
    }
    if (mode == "OwnerDestroyedWithBlockedSinkTerminates")
    {
        // 绕过测试 Owner 的解除屏障析构，直接销毁仍持输出责任的产品对象。
        std::puts("FATAL_READY");
        std::fflush(stdout);
        std::destroy_at(&owner.System);
        std::_Exit(5);
    }
    pg::log::StopReport pending;
    const auto before = std::chrono::steady_clock::now();
    const auto stopped = owner.System.Stop(before + std::chrono::milliseconds(10), pending);
    const bool bounded = !stopped && !pending.ResourcesReleased &&
                         std::chrono::steady_clock::now() - before < std::chrono::milliseconds(500);
    owner.Hooks.BlockOutput.store(false);
    pg::log::StopReport final;
    return bounded && owner.System.Stop(pg::log::test::Deadline(), final) && final.ResourcesReleased &&
                   final.PreviousFlush.has_value()
               ? 0
               : 6;
}
