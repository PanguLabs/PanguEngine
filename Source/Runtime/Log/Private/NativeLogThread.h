#pragma once

#include <LogTypes.h>
#include <PanguLogExport.h>
#include <Result.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

#ifndef _WIN32
#include <pthread.h>
#endif

namespace pg::log::detail
{
// 包括编码临时对象、原生转换/扫描局部对象及库调用余量；不只采用 OS 的最小栈值。
inline constexpr std::size_t kMinimumLogWorkerStackBytes = 128UZ * 1024;
// 只由串行管理端 Start/TryJoin；超时不放弃句柄，析构要求已完成原生回收。
class PG_LOG_API NativeLogThread final
{
public:
    NativeLogThread() noexcept = default;
    ~NativeLogThread();
    NativeLogThread(const NativeLogThread&) = delete;
    NativeLogThread& operator=(const NativeLogThread&) = delete;
    NativeLogThread(NativeLogThread&&) = delete;
    NativeLogThread& operator=(NativeLogThread&&) = delete;
    [[nodiscard]] Status Start(void (*entry)(void*) noexcept, void* context, std::size_t stackBytes) noexcept;
    [[nodiscard]] Status TryJoin(std::chrono::steady_clock::time_point deadline) noexcept;
    [[nodiscard]] bool OwnsResource() const noexcept { return Started; }
    [[nodiscard]] bool IsCurrent() const noexcept;

private:
#ifdef _WIN32
    static unsigned int __stdcall Run(void* context) noexcept;
    void* Handle{};
    unsigned int Id{};
#else
    static void* Run(void* context) noexcept;
    pthread_t Handle{};
#ifdef __APPLE__
    // mach_thread_self 的独立 send right，跨越 pthread 用户代码和 TLS 清理而存活。
    std::atomic<std::uint32_t> CompletionPort;
    bool Joined{};
#endif
#endif
    void (*Entry)(void*) noexcept {};
    void* Context{};
    bool Started{};
};

PG_LOG_API void PauseLogWorker() noexcept;
} // namespace pg::log::detail
