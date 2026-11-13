#include "NativeLogIo.h"
#include "Error.h"
#include "LogTypes.h"
#include "Result.h"
#include <cerrno>
#include <cstdint>
#include <expected>
#include <fcntl.h>
#include <optional>
#include <pthread.h> // IWYU pragma: keep
// POSIX 信号集合/线程掩码属于 C API，不能换成仅提供标准 C++ 信号子集的头。
// NOLINTNEXTLINE(modernize-deprecated-headers)
#include <signal.h>
#include <string_view>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/fcntl.h>
#include <sys/signal.h>
#endif

namespace pg::log::detail
{
Error NativeIoError(std::int64_t code, LogError kind) noexcept
{
    return {kLogErrorDomain,
            static_cast<std::uint32_t>(kind),
            {.Source = NativeErrorSourceId{0x6572726e6fULL}, .Value = code}};
}
Result<NativeHandle> OpenConsoleNative() noexcept
{
    const int descriptor = fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 0);
    if (descriptor < 0)
    {
        return std::unexpected(NativeIoError(errno, LogError::kFileOpenFailed));
    }
    return NativeHandle{descriptor};
}
OutputOutcome WriteNative(NativeHandle handle, std::string_view text, bool console) noexcept
{
    if (text.empty())
    {
        return {.Complete = true};
    }
    // POSIX 公共 signal.h 提供 sigset_t，不能包含 glibc 私有 bits 头。
    // NOLINTNEXTLINE(misc-include-cleaner)
    sigset_t blocked{};
    sigset_t previous{};
    sigset_t pending{};
    bool hadPending{};
    // 只改变当前调用线程的信号掩码，不修改全进程 SIGPIPE disposition。
    if (console)
    {
        sigemptyset(&blocked);
        sigaddset(&blocked, SIGPIPE);
        const int error = pthread_sigmask(SIG_BLOCK, &blocked, &previous);
        if (error != 0)
        {
            return {.Failure = NativeIoError(error)};
        }
        sigpending(&pending);
        hadPending = sigismember(&pending, SIGPIPE) == 1;
    }
    const auto count = write(static_cast<int>(handle.Value), text.data(), text.size());
    const int error = count < 0 ? errno : 0;
    std::optional<Error> failure = count < 0 ? std::optional(NativeIoError(error)) : std::nullopt;
    if (console)
    {
        if (error == EPIPE && !hadPending)
        {
            sigpending(&pending);
            if (sigismember(&pending, SIGPIPE) == 1)
            {
                int consumed{};
                const int waited = sigwait(&blocked, &consumed);
                if (waited != 0 && !failure)
                {
                    failure = NativeIoError(waited);
                }
            }
        }
        const int restored = pthread_sigmask(SIG_SETMASK, &previous, nullptr);
        if (restored != 0 && !failure)
        {
            failure = NativeIoError(restored);
        }
    }
    const auto bytes = count < 0 ? 0ULL : static_cast<std::uint64_t>(count);
    return {
        .BytesWritten = bytes,
        .Failure = failure,
        .Complete = !failure && bytes == text.size(),
        .NativeUnitsWritten = bytes,
    };
}
Status FlushNative(NativeHandle handle, bool console) noexcept
{
    if (console)
    {
        return {};
    }
    if (fsync(static_cast<int>(handle.Value)) != 0)
    {
        return std::unexpected(NativeIoError(errno));
    }
    return {};
}
Status CloseNative(NativeHandle& handle) noexcept
{
    if (!handle.Valid())
    {
        return {};
    }
    const int result = close(static_cast<int>(handle.Value));
    const int error = result == 0 ? 0 : errno;
    // Linux 先释放 fd 再报告延迟错误。Darwin fp_close_and_unlock 同样先 fdrelse 再 fg_drop/fo_close；
    // 这里不支持线程取消，EINTR 也不重试已释放编号，避免关闭其他线程刚复用的 fd。
    handle = {};
    if (result != 0)
    {
        return std::unexpected(NativeIoError(error));
    }
    return {};
}
} // namespace pg::log::detail
