#include "NativeLogThread.h"
#include "NativeLogIo.h"
#include <LogTypes.h>
#include <Result.h>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <expected>
#include <pthread.h>

#ifdef __APPLE__
#include <Error.h>
#include <atomic>
#include <cstdint>
#include <mach/kern_return.h>
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#include <mach/port.h>
#endif

namespace pg::log::detail
{
#ifdef __APPLE__
namespace
{
// kern_return.h 是公开入口，不为 typedef 的平台实现细节包含 machine 私有头。
// NOLINTNEXTLINE(misc-include-cleaner)
Error MachThreadError(kern_return_t code) noexcept
{
    return {kLogErrorDomain,
            static_cast<std::uint32_t>(LogError::kIoFailed),
            {.Source = NativeErrorSourceId{0x6d616368ULL}, .Value = code}};
}
} // namespace
#endif
void* NativeLogThread::Run(void* context) noexcept
{
    // Apple 分支要发布拥有的 Mach right；其他 POSIX 目标保持同一入口实现。
    // NOLINTNEXTLINE(misc-const-correctness)
    auto& thread = *static_cast<NativeLogThread*>(context);
#ifdef __APPLE__
    thread.CompletionPort.store(mach_thread_self(), std::memory_order_release);
#endif
    thread.Entry(thread.Context);
    return nullptr;
}
Status NativeLogThread::Start(void (*entry)(void*) noexcept, void* context, std::size_t stackBytes) noexcept
{
    if (Started || entry == nullptr || stackBytes == 0)
    {
        return std::unexpected(MakeLogError(LogError::kInvalidConfig));
    }
    // pthread.h 是公开入口；不为 include-cleaner 引入各 libc 的私有 typedef 头。
    // NOLINTNEXTLINE(misc-include-cleaner)
    pthread_attr_t attributes{};
    auto error = pthread_attr_init(&attributes);
    if (error != 0)
    {
        return std::unexpected(NativeIoError(error, LogError::kThreadStartFailed));
    }
    error = pthread_attr_setstacksize(&attributes, stackBytes);
    if (error == 0)
    {
        Entry = entry;
        Context = context;
        error = pthread_create(&Handle, &attributes, Run, this);
        Started = error == 0;
    }
    const auto destroyed = pthread_attr_destroy(&attributes);
    if (error != 0)
    {
        return std::unexpected(NativeIoError(error, LogError::kThreadStartFailed));
    }
    if (destroyed != 0)
    {
        return std::unexpected(NativeIoError(destroyed, LogError::kThreadStartFailed));
    }
    return {};
}
bool NativeLogThread::IsCurrent() const noexcept
{
#ifdef __APPLE__
    if (Joined)
    {
        return false;
    }
#endif
    return Started && pthread_equal(Handle, pthread_self()) != 0;
}
Status NativeLogThread::TryJoin(std::chrono::steady_clock::time_point deadline) noexcept
{
    if (!Started)
    {
        return {};
    }
    if (IsCurrent())
    {
        return std::unexpected(MakeLogError(LogError::kBusy));
    }
    for (;;)
    {
#ifdef __APPLE__
        const auto port = CompletionPort.load(std::memory_order_acquire);
        mach_port_type_t type{};
        const auto queried = port == MACH_PORT_NULL ? KERN_SUCCESS : mach_port_type(mach_task_self(), port, &type);
        if (queried != KERN_SUCCESS)
        {
            return std::unexpected(MachThreadError(queried));
        }
        if ((type & MACH_PORT_TYPE_DEAD_NAME) != 0)
        {
            // 内核端口已死亡：libpthread 的 exit_gate 已置 DEAD，join 只走已退出回收分支。
            // 不依据业务标志调用等待式 join；官方 libpthread/XNU 依据见 Task7 验收报告。
            if (!Joined)
            {
                const auto joined = pthread_join(Handle, nullptr);
                if (joined != 0)
                {
                    return std::unexpected(NativeIoError(joined));
                }
                Joined = true;
            }
            const auto released = mach_port_deallocate(mach_task_self(), port);
            if (released != KERN_SUCCESS)
            {
                return std::unexpected(MachThreadError(released));
            }
            CompletionPort.store(MACH_PORT_NULL, std::memory_order_release);
            Started = false;
            return {};
        }
#elifdef __ANDROID__
        // bionic 没有 tryjoin；joinable 句柄保留至回收，内核清零 tid 后 join 不再等待退出。
        // Android 10 与当前 AOSP 的 gettid/join 依据见正式诊断验收；不使用业务完成标志。
        if (pthread_gettid_np(Handle) == 0)
        {
            const auto joined = pthread_join(Handle, nullptr);
            if (joined != 0)
            {
                return std::unexpected(NativeIoError(joined));
            }
            Started = false;
            return {};
        }
#else
        const auto joined = pthread_tryjoin_np(Handle, nullptr);
        if (joined == 0)
        {
            Started = false;
            return {};
        }
        if (joined != EBUSY)
        {
            return std::unexpected(NativeIoError(joined));
        }
#endif
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return std::unexpected(MakeLogError(LogError::kBusy));
        }
        PauseLogWorker();
    }
}
} // namespace pg::log::detail
