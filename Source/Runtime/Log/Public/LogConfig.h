#pragma once

#include <LogRecord.h>
#include <LogTypes.h>
#include <PanguLogExport.h>
#include <Result.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace pg::log
{
using NativePathView = std::basic_string_view<std::filesystem::path::value_type>;
inline constexpr std::size_t kLogPathCapacity = 1024;
inline constexpr std::size_t kMaxLogQueueCapacity = 4096;
inline constexpr std::size_t kMaxLogPriorityReserve = 512;

struct LogConfig
{
    // 仅借用 Initialize 调用；实现须在启动线程/发布 Writer 前复制到有界存储。
    NativePathView Directory;
    std::array<std::byte, 16> PartitionId{};
    OutputMask Enabled{OutputMask::kConsole | OutputMask::kFile};
    OutputMask Required{OutputMask::kNone};
    Level Minimum{Level::kInfo};
    std::size_t QueueCapacity{256};
    std::size_t PriorityReserve{32};
    std::size_t MemoryBudgetBytes{1024UZ * 1024};
    std::size_t WorkerStackBytes{256UZ * 1024};
    std::uint64_t PartitionBytes{64ULL * 1024 * 1024};
    std::uint64_t FileBytes{4ULL * 1024 * 1024};
    std::chrono::milliseconds DefaultWait{100};
};

// 由真实运行时提供：槽含元数据/对齐；FixedBytes 含控制块、在途记录、编码缓冲、统计/快照等。
// WorkerStackBytes 在 RequiredLogMemory 中另计一次，不能再次藏在 FixedBytes 中。
struct LogMemoryLayout
{
    std::size_t QueueSlotBytes{};
    std::size_t FixedBytes{};
};

[[nodiscard]] PG_LOG_API Result<std::size_t> RequiredLogMemory(const LogConfig& config,
                                                               LogMemoryLayout layout) noexcept;
// 只验证实例级矛盾与预算；成功不代表任何输出已 Ready。
[[nodiscard]] PG_LOG_API Status ValidateLogConfig(const LogConfig& config, LogMemoryLayout layout) noexcept;
// 先通过实例校验，再独立评估 File。失败记入 File 能力，初始化按 Required 决定降级或回滚。
// 禁用 File 时不要求目录/身份；不访问文件系统，也不尝试打开或猜测其他路径。
[[nodiscard]] PG_LOG_API Status ValidateLogFileConfig(const LogConfig& config) noexcept;
} // namespace pg::log
