#include "ConsumerPause.h"
#include "LogConfig.h"
#include "LogReports.h"
#include "LogTypes.h"
#include "Result.h"
#include <Log.h>
#include <LogSystem.h>
#include <LogTestAccess.h>
#include <NativeLogThread.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>

namespace
{
bool WaitFor(const std::atomic<bool>& flag)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!flag.load())
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}
} // namespace

// main 保持标准签名，argc 检查保护唯一一次 C ABI 数组访问。
// NOLINTNEXTLINE(misc-const-correctness)
int main(int argc, char** argv)
{
    if (argc != 2)
    {
        return 1;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const std::string_view mode{argv[1]};
    if (mode == "control-exit42" || mode == "control-crash" || mode == "control-hang")
    {
        // 负对照先提供相同握手，但绝不调用产品 Fatal，检验监督是否只看非零退出。
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
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    if (mode == "native-exit")
    {
        struct ExitState
        {
            std::atomic<bool> BodyFinished;
            std::atomic<bool> DestructorEntered;
            std::atomic<bool> Release;
        } state;
        struct ExitGuard
        {
            explicit ExitGuard(ExitState& state) noexcept
                : State(&state)
            {
            }
            ExitGuard(const ExitGuard&) = delete;
            ExitGuard& operator=(const ExitGuard&) = delete;
            ExitGuard(ExitGuard&&) = delete;
            ExitGuard& operator=(ExitGuard&&) = delete;
            ExitState* State{};
            ~ExitGuard()
            {
                State->DestructorEntered.store(true);
                while (!State->Release.load())
                {
                    std::this_thread::yield();
                }
            }
        };
        pg::log::detail::NativeLogThread worker;
        const auto entry = [](void* context) noexcept
        {
            auto& exit = *static_cast<ExitState*>(context);
            thread_local const ExitGuard kGuard{exit};
            static_cast<void>(kGuard);
            exit.BodyFinished.store(true);
        };
        if (!worker.Start(entry, &state, 256UZ * 1024))
        {
            return 20;
        }
        if (!WaitFor(state.DestructorEntered) || !state.BodyFinished.load())
        {
            std::_Exit(21);
        }
        const auto before = std::chrono::steady_clock::now();
        const auto first = worker.TryJoin(before + std::chrono::milliseconds(10));
        const bool pending = !first && worker.OwnsResource() &&
                             std::chrono::steady_clock::now() - before < std::chrono::milliseconds(500);
        state.Release.store(true);
        if (!worker.TryJoin(std::chrono::steady_clock::now() + std::chrono::seconds(2)))
        {
            std::_Exit(22);
        }
        return pending && !worker.OwnsResource() ? 0 : 23;
    }
    pg::log::detail::LogTestHooks hooks;
    pg::log::LogSystem system;
    pg::log::detail::LogTestAccess::Attach(system, hooks);
    pg::log::LogConfig config;
    config.Enabled = pg::log::OutputMask::kConsole;
    std::filesystem::path directory;
    if (mode == "minimum-stack")
    {
        directory = std::filesystem::path{PG_LOG_INIT_TEST_ROOT} / "minimum-stack";
        std::filesystem::create_directories(directory);
        config.Directory = directory.native();
        config.Enabled = pg::log::OutputMask::kConsole | pg::log::OutputMask::kFile;
        config.Required = config.Enabled;
        config.PartitionId.front() = std::byte{4};
        config.WorkerStackBytes = 128UZ * 1024;
        config.FileBytes = 13312;
    }
    pg::log::LogInitReport report;
    if (!system.Initialize(config, report))
    {
        return 2;
    }
    auto writer = system.GetWriter();
    if (mode == "minimum-stack")
    {
        hooks.BlockOutput.store(true);
        const std::string message(1024, 'a');
        if (pg::log::test::SubmitPaused(system, writer, [&](const auto& pausedWriter)
                                        { return PG_LOG_ERROR(pausedWriter, "stack", "{}", message); }) !=
                pg::log::SubmitResult::kAccepted ||
            !WaitFor(hooks.OutputEntered))
        {
            return 39;
        }
        for (int record = 0; record < 20; ++record)
        {
            if (pg::log::test::SubmitPaused(system, writer, [&](const auto& pausedWriter)
                                            { return PG_LOG_ERROR(pausedWriter, "stack", "{}", message); }) !=
                pg::log::SubmitResult::kAccepted)
            {
                return 40;
            }
        }
        hooks.BlockOutput.store(false);
        pg::log::StopReport stopped;
        if (!system.Stop(std::chrono::steady_clock::now() + std::chrono::seconds(2), stopped))
        {
            return 41;
        }
        if (stopped.FinalFlush.H != 21 || stopped.FinalFlush.Outputs.at(0).Delivered != 21 ||
            stopped.FinalFlush.Outputs.at(1).Delivered != 21)
        {
            return 42;
        }
        // 前20个原生字符包含本次session及分隔符，不将目录中的历史会话算作本次轮转。
        const auto sessionPrefix = std::filesystem::path{report.FilePath.View()}.filename().native().substr(0, 20);
        std::size_t files{};
        for (const auto& file : std::filesystem::directory_iterator(directory))
        {
            if (file.path().extension() == ".log" && file.path().filename().native().starts_with(sessionPrefix))
            {
                ++files;
            }
        }
        return files >= 3 && stopped.FinalFlush.OutputComplete ? 0 : 43;
    }
    if (mode == "fatal-lock")
    {
        if (!pg::log::detail::LogTestAccess::TryHoldSnapshot(system))
        {
            return 38;
        }
        std::puts("FATAL_READY");
        std::fflush(stdout);
        return 0;
    }
    if (mode == "snapshot-deadline")
    {
        if (!pg::log::detail::LogTestAccess::TryHoldSnapshot(system))
        {
            return 30;
        }
        std::atomic<bool> returned{};
        pg::Status outcome;
        std::jthread stopping(
            [&]
            {
                pg::log::StopReport stopped;
                outcome = system.Stop(std::chrono::steady_clock::now() + std::chrono::milliseconds(10), stopped);
                returned.store(true);
            });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
        while (!returned.load() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        const bool bounded = returned.load();
        pg::log::detail::LogTestAccess::ReleaseSnapshot(system);
        stopping.join();
        pg::log::StopReport stopped;
        if (!system.Stop(std::chrono::steady_clock::now() + std::chrono::seconds(2), stopped))
        {
            return 31;
        }
        return bounded && !outcome ? 0 : 32;
    }
    if (mode == "closing-state")
    {
        hooks.BlockOutput.store(true);
        if (pg::log::test::SubmitPaused(system, writer, [&](const auto& pausedWriter)
                                        { return PG_LOG_INFO(pausedWriter, "closing", "first"); }) !=
                pg::log::SubmitResult::kAccepted ||
            !WaitFor(hooks.OutputEntered))
        {
            return 33;
        }
        std::atomic<bool> entered{};
        std::atomic<bool> release{};
        std::jthread producer(
            [&]
            {
                static_cast<void>(PG_LOG_INFO(writer, "closing", "{}",
                                              (
                                                  [&]
                                                  {
                                                      entered.store(true);
                                                      while (!release.load())
                                                      {
                                                          std::this_thread::yield();
                                                      }
                                                      return 19;
                                                  }())));
            });
        if (!WaitFor(entered))
        {
            std::_Exit(34);
        }
        pg::log::StopReport stopped;
        if (system.Stop(std::chrono::steady_clock::now() + std::chrono::milliseconds(10), stopped))
        {
            std::_Exit(35);
        }
        hooks.BlockOutput.store(false);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (system.Inspect().HighestProcessed != 1 && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        const bool closing = system.GetState() == pg::log::LogState::kClosing;
        bool controlled{};
        {
            const pg::log::test::ConsumerPause pause{system};
            controlled = pause.Owns();
            release.store(true);
            producer.join();
        }
        if (!system.Stop(std::chrono::steady_clock::now() + std::chrono::seconds(2), stopped))
        {
            return 36;
        }
        return controlled && closing && stopped.FinalFlush.H == 2 ? 0 : 37;
    }
    if (mode == "active")
    {
        std::atomic<bool> entered{};
        std::atomic<bool> release{};
        pg::log::SubmitResult result{};
        std::jthread producer(
            [&]
            {
                result = PG_LOG_INFO(writer, "active", "{}",
                                     (
                                         [&]
                                         {
                                             entered.store(true);
                                             while (!release.load())
                                             {
                                                 std::this_thread::yield();
                                             }
                                             return 73;
                                         }()));
            });
        if (!WaitFor(entered))
        {
            std::_Exit(3);
        }
        pg::log::StopReport stopped;
        const auto before = std::chrono::steady_clock::now();
        const auto first = system.Stop(before + std::chrono::milliseconds(10), stopped);
        const bool held = !first && system.GetState() == pg::log::LogState::kClosing && !stopped.ResourcesReleased &&
                          hooks.DataBlocks.load() == 1 && hooks.ControlOwners.load() == 1;
        bool controlled{};
        {
            const pg::log::test::ConsumerPause pause{system};
            controlled = pause.Owns();
            release.store(true);
            producer.join();
        }
        if (!controlled || !held || result != pg::log::SubmitResult::kAccepted)
        {
            return 4;
        }
        if (!system.Stop(std::chrono::steady_clock::now() + std::chrono::seconds(2), stopped))
        {
            return 5;
        }
        if (stopped.FinalFlush.H != 1 || stopped.FinalFlush.Outputs.at(0).Delivered != 1)
        {
            return 6;
        }
        if (PG_LOG_INFO(writer, "stale", "closed") != pg::log::SubmitResult::kClosed)
        {
            return 7;
        }
        return hooks.DataBlocks.load() == 0 && hooks.WorkerHandles.load() == 0 ? 0 : 8;
    }
    if (mode == "blocked" || mode == "fatal")
    {
        hooks.BlockOutput.store(true);
        if (pg::log::test::SubmitPaused(system, writer, [&](const auto& pausedWriter)
                                        { return PG_LOG_INFO(pausedWriter, "blocked", "owned"); }) !=
            pg::log::SubmitResult::kAccepted)
        {
            return 9;
        }
        if (!WaitFor(hooks.OutputEntered))
        {
            return 10;
        }
        pg::log::StopReport stopped;
        const auto before = std::chrono::steady_clock::now();
        const auto result = system.Stop(before + std::chrono::milliseconds(10), stopped);
        if (result || stopped.ResourcesReleased ||
            std::chrono::steady_clock::now() - before > std::chrono::milliseconds(500))
        {
            return 11;
        }
        const auto snapshot = system.Inspect();
        if (snapshot.State != pg::log::LogState::kClosing || hooks.DataBlocks.load() != 1)
        {
            return 12;
        }
        if (mode == "fatal")
        {
            std::puts("FATAL_READY");
            std::fflush(stdout);
            return 0;
        }
        hooks.BlockOutput.store(false);
        if (!system.Stop(std::chrono::steady_clock::now() + std::chrono::seconds(2), stopped))
        {
            return 13;
        }
        return stopped.FinalFlush.Outputs.at(0).Delivered == 1 ? 0 : 14;
    }
    if (mode == "native-owner-exit")
    {
        hooks.BlockExit.store(true);
        pg::log::StopReport stopped;
        if (system.Stop(std::chrono::steady_clock::now() + std::chrono::milliseconds(10), stopped))
        {
            return 24;
        }
        if (!WaitFor(hooks.ExitEntered))
        {
            return 25;
        }
        const bool held = hooks.WorkerHandles.load() == 1 && hooks.DataBlocks.load() == 1 && !stopped.ResourcesReleased;
        hooks.BlockExit.store(false);
        if (!system.Stop(std::chrono::steady_clock::now() + std::chrono::seconds(2), stopped))
        {
            return 26;
        }
        return held && hooks.WorkerHandles.load() == 0 && hooks.DataBlocks.load() == 0 ? 0 : 27;
    }
    return 15;
}
