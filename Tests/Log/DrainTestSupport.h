#pragma once

#include <Log.h>
#include <LogTestAccess.h>

#include <chrono>
#include <string>
#include <thread>

namespace pg::log::test
{
inline auto Deadline() { return std::chrono::steady_clock::now() + std::chrono::seconds(2); }
template <typename Predicate>
bool Wait(Predicate predicate)
{
    const auto deadline = Deadline();
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
inline Status Flush(LogSystem& system, std::chrono::steady_clock::time_point deadline, FlushReport& report)
{
    return system.Flush(deadline, report);
}
struct DrainOwner
{
    detail::LogTestHooks Hooks;
    LogSystem System;
    DrainOwner() { detail::LogTestAccess::Attach(System, Hooks); }
    ~DrainOwner()
    {
        Hooks.BlockOutput.store(false);
        Hooks.BlockQueuePop.store(false);
        Hooks.BlockSequence.store(0);
    }
    DrainOwner(const DrainOwner&) = delete;
    DrainOwner& operator=(const DrainOwner&) = delete;
    DrainOwner(DrainOwner&&) = delete;
    DrainOwner& operator=(DrainOwner&&) = delete;
    Status Start(std::size_t capacity = 256)
    {
        LogConfig config;
        config.Enabled = OutputMask::kConsole;
        config.QueueCapacity = capacity;
        config.PriorityReserve = 0;
        LogInitReport report;
        return System.Initialize(config, report);
    }
};
} // namespace pg::log::test
