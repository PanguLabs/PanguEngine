#pragma once

#include <LogReports.h>
#include <LogWriter.h>
#include <PanguLogExport.h>

#include <atomic>
#include <chrono>

namespace pg::log
{
namespace detail
{
struct LogSystemData;
struct LogTestAccess;
struct LogTestHooks;
} // namespace detail

// 管理操作由拥有者串行调用；Inspect/GetWriter 和提交可并发。
// Stop 超时仍持有全部责任；拥有者必须重试至 ResourcesReleased 后才能销毁。
class PG_LOG_API LogSystem final
{
public:
    LogSystem() noexcept = default;
    ~LogSystem();
    LogSystem(const LogSystem&) = delete;
    LogSystem& operator=(const LogSystem&) = delete;
    LogSystem(LogSystem&&) = delete;
    LogSystem& operator=(LogSystem&&) = delete;

    [[nodiscard]] Status Initialize(const LogConfig& config, LogInitReport& report) noexcept;
    [[nodiscard]] LogState GetState() const noexcept;
    [[nodiscard]] LogWriter GetWriter(LogTags tags = {}) const noexcept;
    [[nodiscard]] LogSnapshot Inspect() const noexcept;
    [[nodiscard]] Status Flush(std::chrono::steady_clock::time_point deadline, FlushReport& report) noexcept;
    [[nodiscard]] Status Stop(std::chrono::steady_clock::time_point deadline, StopReport& report) noexcept;

private:
    friend struct detail::LogTestAccess;
    friend struct detail::LogSystemData;
    void Publish() noexcept;
    void FinishWithoutWorker() noexcept;
    void ReleaseData() noexcept;
    [[nodiscard]] Status Rollback(Error error, LogInitReport& report) noexcept;
    mutable std::atomic_flag SnapshotLock;
    std::atomic_flag ManagementLock;
    detail::LogSystemData* Data{};
    detail::LogTestHooks* Hooks{};
    LogSnapshot Snapshot;
    StopReport FinalStop;
    std::chrono::milliseconds DefaultWait{100};
};
} // namespace pg::log
