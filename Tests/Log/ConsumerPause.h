#pragma once

#include <LogSystem.h>
#include <LogTestAccess.h>

#include <chrono>
#include <thread>

namespace pg::log::test
{
// 消费者在同一 SnapshotLock 内尝试 TryPop；取得此锁后，提交不会撞到空轮询的队列锁。
// 仅测试持有短段：先取 Writer，锁内只提交，解锁后再 Inspect/Flush/Stop。
class ConsumerPause final
{
public:
    explicit ConsumerPause(LogSystem& system) noexcept
        : System(system)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!(Held = detail::LogTestAccess::TryHoldSnapshot(System)))
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return;
            }
            std::this_thread::yield();
        }
    }
    ~ConsumerPause()
    {
        if (Held)
        {
            detail::LogTestAccess::ReleaseSnapshot(System);
        }
    }
    ConsumerPause(const ConsumerPause&) = delete;
    ConsumerPause& operator=(const ConsumerPause&) = delete;
    ConsumerPause(ConsumerPause&&) = delete;
    ConsumerPause& operator=(ConsumerPause&&) = delete;
    [[nodiscard]] bool Owns() const noexcept { return Held; }

private:
    LogSystem& System;
    bool Held{};
};

// Writer 参数在加锁前取得；锁内仅执行一次真实提交，不重试合法拒收。
template <typename Submit>
SubmitResult SubmitPaused(LogSystem& system, const LogWriter& writer, Submit submit) noexcept
{
    const ConsumerPause pause{system};
    return pause.Owns() ? submit(writer) : SubmitResult::kDropped;
}
} // namespace pg::log::test
