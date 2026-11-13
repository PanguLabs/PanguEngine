#pragma once
#include <LogQueue.h>
#include <WriterControl.h>

namespace pg::log::detail
{
// 仅测试可注入边界状态；生产接口不提供改写序号和统计的入口。
struct WriterTestAccess
{
    static void Lock(LogQueue& queue) { static_cast<void>(queue.Lock.test_and_set()); }
    static void Unlock(LogQueue& queue) { queue.Lock.clear(); }
    static void SeedSequence(LogQueue& queue, std::uint64_t value) { queue.HighestAccepted = value; }
    static void SeedCapacityCounter(LogQueue& queue, Level level, std::uint64_t value)
    {
        queue.Rejected.at(static_cast<std::size_t>(level)).Capacity.store(value);
    }
    static std::uint64_t SetReferences(WriterControlOwner& owner, std::uint64_t value)
    {
        return owner.Control->References.exchange(value);
    }
};
} // namespace pg::log::detail
