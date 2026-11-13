#include "NativeLogThread.h"
#include "LogTypes.h"
#include "NativeLogIo.h"
#include "Result.h"
#include <Error.h>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#define NOMINMAX
#include <Windows.h> // IWYU pragma: keep
#include <errhandlingapi.h>
#include <expected>
#include <handleapi.h>
#include <processthreadsapi.h>
#include <synchapi.h>
#include <winbase.h>
#include <winerror.h>

#include <process.h>

#include <limits>

namespace pg::log::detail
{
unsigned int __stdcall NativeLogThread::Run(void* context) noexcept
{
    const auto& thread = *static_cast<NativeLogThread*>(context);
    thread.Entry(thread.Context);
    return 0;
}
Status NativeLogThread::Start(void (*entry)(void*) noexcept, void* context, std::size_t stackBytes) noexcept
{
    if (Started || entry == nullptr || stackBytes == 0 || stackBytes > std::numeric_limits<unsigned int>::max())
    {
        return std::unexpected(MakeLogError(LogError::kInvalidConfig));
    }
    Entry = entry;
    Context = context;
    const auto handle = _beginthreadex(nullptr, static_cast<unsigned int>(stackBytes), Run, this,
                                       STACK_SIZE_PARAM_IS_A_RESERVATION, &Id);
    if (handle == 0)
    {
        return std::unexpected(Error{kLogErrorDomain,
                                     static_cast<std::uint32_t>(LogError::kThreadStartFailed),
                                     {.Source = NativeErrorSourceId{0x6572726e6fULL}, .Value = errno}});
    }
    // Win32/CRT 边界：整数返回值为调用方拥有的 HANDLE。
    // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
    Handle = reinterpret_cast<void*>(handle);
    Started = true;
    return {};
}
bool NativeLogThread::IsCurrent() const noexcept { return Started && Id == GetCurrentThreadId(); }
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
        // 只查询已终止的原生线程对象；不把 Entry 返回当成 CRT/DLL 清理完成。
        const auto wait = WaitForSingleObject(Handle, 0);
        if (wait == WAIT_OBJECT_0)
        {
            if (CloseHandle(Handle) == 0)
            {
                return std::unexpected(NativeIoError(GetLastError()));
            }
            Handle = nullptr;
            Started = false;
            return {};
        }
        if (wait != WAIT_TIMEOUT)
        {
            return std::unexpected(NativeIoError(GetLastError()));
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return std::unexpected(MakeLogError(LogError::kBusy));
        }
        PauseLogWorker();
    }
}
} // namespace pg::log::detail
