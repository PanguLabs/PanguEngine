#include "../ConsumerPause.h"
#include <Check.h>
#include <FailureContext.h>
#include <FailureReport.h>
#include <Log.h>
#include <LogConfig.h>
#include <LogReports.h>
#include <LogSystem.h>
#include <LogTestAccess.h>
#include <LogTypes.h>
#include <LogWriter.h>
#include <benchmark/utils.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <numeric>
#include <ratio>
#include <system_error>
#include <thread>
#include <vector>

namespace
{
using namespace pg::log;
using Clock = std::chrono::steady_clock;
struct Owner
{
    detail::LogTestHooks Hooks;
    LogSystem System;
    Owner() { detail::LogTestAccess::Attach(System, Hooks); }
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;
    Owner(Owner&&) = delete;
    Owner& operator=(Owner&&) = delete;
    ~Owner()
    {
        Hooks.BlockSequence.store(0);
        StopReport report;
        PG_CHECK(System.Stop(Clock::now() + std::chrono::seconds(5), report) && report.ResourcesReleased,
                 "基准必须安全停止真实实例");
    }
    void Start(const LogConfig& config)
    {
        LogInitReport report;
        PG_CHECK(System.Initialize(config, report), "基准初始化失败");
    }
    void Freeze(const LogWriter& writer)
    {
        {
            const test::ConsumerPause pause(System);
            PG_CHECK(pause.Owns(), "预热消费者屏障超时");
            PG_CHECK(PG_LOG_ERROR(writer, "benchmark", "warmup") == SubmitResult::kAccepted, "预热未接纳");
        }
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        while (!Hooks.SequenceEntered.load() && Clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        PG_CHECK(Hooks.SequenceEntered.load(), "消费者屏障未到达");
    }
};
template <class Work>
void Measure(const char* name, std::size_t count, Work work)
{
    std::vector<double> samples(count);
    for (auto& sample : samples)
    {
        const auto start = Clock::now();
        // DoNotOptimize 的可变左值重载是基准库要求，避免 const 重载被折叠。
        // NOLINTNEXTLINE(misc-const-correctness)
        bool valid = work();
        benchmark::DoNotOptimize(valid);
        const auto end = Clock::now();
        PG_CHECK(valid, "基准走入了错误状态");
        sample = std::chrono::duration<double, std::nano>(end - start).count();
    }
    const double mean = std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(count);
    std::ranges::sort(samples);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,modernize-use-std-print)
    std::printf("BENCH %s n=%zu mean=%.2f p50=%.2f p95=%.2f p99=%.2f ns\n", name, count, mean, samples.at(count / 2),
                samples.at((count * 95) / 100), samples.at((count * 99) / 100));
}
void SubmissionCases()
{
    {
        Owner owner;
        LogConfig config;
        config.Enabled = OutputMask::kNone;
        owner.Start(config);
        const auto writer = owner.System.GetWriter();
        Measure("Disabled", 10000,
                [&] { return PG_LOG_INFO(writer, "benchmark", "{}", 42) == SubmitResult::kDisabled; });
    }
    {
        Owner owner;
        owner.Hooks.BlockSequence.store(1);
        LogConfig config;
        config.Enabled = OutputMask::kConsole;
        config.QueueCapacity = 4096;
        config.PriorityReserve = 32;
        config.MemoryBudgetBytes = 16UZ * 1024 * 1024;
        owner.Start(config);
        const auto writer = owner.System.GetWriter();
        owner.Freeze(writer);
        Measure("Accepted", 2000,
                [&] { return PG_LOG_INFO(writer, "benchmark", "{}", 42) == SubmitResult::kAccepted; });
    }
    for (const bool contention : {false, true})
    {
        Owner owner;
        owner.Hooks.BlockSequence.store(1);
        LogConfig config;
        config.Enabled = OutputMask::kConsole;
        config.QueueCapacity = 8;
        config.PriorityReserve = 1;
        owner.Start(config);
        const auto writer = owner.System.GetWriter();
        owner.Freeze(writer);
        if (contention)
        {
            detail::LogTestAccess::HoldQueue(owner.System);
        }
        else
        {
            for (std::size_t index = 0; index < config.QueueCapacity; ++index)
            {
                PG_CHECK(PG_LOG_ERROR(writer, "benchmark", "{}", index) == SubmitResult::kAccepted, "填队列失败");
            }
        }
        Measure(contention ? "ContentionDrop" : "FullDrop", 10000,
                [&] { return PG_LOG_ERROR(writer, "benchmark", "{}", 42) == SubmitResult::kDropped; });
        if (contention)
        {
            detail::LogTestAccess::ReleaseQueue(owner.System);
        }
    }
}
} // namespace
int main()
{
    SubmissionCases();
    Measure("FailureReport", 10000,
            []
            {
                pg::FailureReport report;
                pg::FailureContext(report).SetPrimary(MakeLogError(LogError::kIoFailed), "benchmark failure");
                // NOLINTNEXTLINE(misc-const-correctness) 同上，使用基准库可变左值保护。
                auto copy = report;
                const auto site = copy.PrimarySite();
                benchmark::DoNotOptimize(copy);
                return site && site->Message() == "benchmark failure";
            });
    const std::filesystem::path directory(PG_LOG_BENCHMARK_ROOT);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    PG_CHECK(!error, "基准目录创建失败");
    const auto& native = directory.native();
    Owner owner;
    LogConfig config;
    config.Directory = native;
    config.PartitionId.front() = std::byte{0x13};
    config.Required = config.Enabled;
    owner.Start(config);
    const auto writer = owner.System.GetWriter();
    Measure("DualOutputAndFlush", 100,
            [&]
            {
                SubmitResult submitted{SubmitResult::kDisabled};
                {
                    const test::ConsumerPause pause(owner.System);
                    if (!pause.Owns())
                    {
                        return false;
                    }
                    submitted = PG_LOG_INFO(writer, "benchmark", "{} {}", 42, 1.25);
                }
                FlushReport report;
                return submitted == SubmitResult::kAccepted &&
                       owner.System.Flush(Clock::now() + std::chrono::seconds(5), report) && report.OutputComplete;
            });
}
