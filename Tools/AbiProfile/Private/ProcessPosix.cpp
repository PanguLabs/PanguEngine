#include "Process.h"
#include "ToolReport.h"
#include <Result.h>

#include <signal.h> // NOLINT(modernize-deprecated-headers) - POSIX kill is outside the C++ signal API.
#include <spawn.h>
#include <sys/poll.h>
#include <sys/types.h> // IWYU pragma: keep - public POSIX pid_t header (Apple reexports its definition).
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <expected>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef __APPLE__
#include <crt_externs.h>
#include <sys/fcntl.h>
#include <sys/signal.h>
#include <sys/spawn.h>
#else
#include <fcntl.h>
#endif

namespace pg::abi
{
namespace
{
class Descriptor
{
public:
    explicit Descriptor(int value = -1) noexcept
        : Value(value)
    {
    }
    ~Descriptor() { Reset(); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    Descriptor(Descriptor&& other) noexcept
        : Value(std::exchange(other.Value, -1))
    {
    }
    Descriptor& operator=(Descriptor&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            Value = std::exchange(other.Value, -1);
        }
        return *this;
    }
    [[nodiscard]] int Get() const noexcept { return Value; }
    void Reset(int value = -1) noexcept
    {
        if (Value >= 0)
        {
            (void)::close(Value);
        }
        Value = value;
    }

private:
    int Value;
};

struct Pipe
{
    Descriptor Read;
    Descriptor Write;
};

[[nodiscard]] bool OpenPipe(Pipe& pipe)
{
    std::array<int, 2> descriptors{};
    if (::pipe(descriptors.data()) != 0)
    {
        return false;
    }
    pipe.Read.Reset(descriptors.front());
    pipe.Write.Reset(descriptors.back());
    // fcntl is the native variadic descriptor configuration API.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
    return ::fcntl(pipe.Read.Get(), F_SETFD, FD_CLOEXEC) == 0 && ::fcntl(pipe.Write.Get(), F_SETFD, FD_CLOEXEC) == 0 &&
           ::fcntl(pipe.Read.Get(), F_SETFL, O_NONBLOCK) == 0;
    // NOLINTEND(cppcoreguidelines-pro-type-vararg)
}

[[nodiscard]] bool Drain(Pipe& pipe, std::string& output, const ProcessRequest& request)
{
    std::array<char, 8192> buffer{};
    for (int batch = 0; batch < 8 && pipe.Read.Get() >= 0; ++batch)
    {
        const auto size = ::read(pipe.Read.Get(), buffer.data(), buffer.size());
        if (size == 0)
        {
            pipe.Read.Reset();
            break;
        }
        if (size < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        const auto count = static_cast<std::size_t>(size);
        if (count > request.CaptureLimitBytes - output.size())
        {
            errno = EOVERFLOW;
            return false;
        }
        output.append(buffer.data(), count);
    }
    return true;
}

struct SpawnConfiguration
{
    SpawnConfiguration() = default;
    SpawnConfiguration(const SpawnConfiguration&) = delete;
    SpawnConfiguration& operator=(const SpawnConfiguration&) = delete;
    SpawnConfiguration(SpawnConfiguration&&) = delete;
    SpawnConfiguration& operator=(SpawnConfiguration&&) = delete;
    posix_spawn_file_actions_t Actions{};
    posix_spawnattr_t Attributes{};
    bool ActionsInitialized{};
    bool AttributesInitialized{};
    ~SpawnConfiguration()
    {
        if (ActionsInitialized)
        {
            (void)::posix_spawn_file_actions_destroy(&Actions);
        }
        if (AttributesInitialized)
        {
            (void)::posix_spawnattr_destroy(&Attributes);
        }
    }
};

class Child
{
public:
    // Apple reexports pid_t through the public sys/types.h header.
    // NOLINTNEXTLINE(misc-include-cleaner)
    explicit Child(pid_t value) noexcept
        : Value(value)
    {
    }
    ~Child()
    {
        (void)::kill(-Value, SIGKILL);
        if (!Reaped)
        {
            int status = 0;
            while (::waitpid(Value, &status, 0) < 0 && errno == EINTR)
            {
            }
        }
    }
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
    Child(Child&&) = delete;
    Child& operator=(Child&&) = delete;
    pid_t Value;
    bool Reaped{};
};

// Only this forked relay can block on downstream. The supervising process never
// writes to a relay pipe: each relay directly reads the compiler's existing pipe.
// After fork, use only stack storage and async-signal-safe native operations.
[[noreturn]] void ForwardBytes(int source, int destination, int unusedRead)
{
    if (unusedRead >= 0)
    {
        (void)::close(unusedRead);
    }
    // Local to the relay, after the real compiler was spawned. EPIPE becomes a
    // normal relay failure without changing parent/compiler signal semantics.
    (void)::signal(SIGPIPE, SIG_IGN);
    std::array<char, 8192> buffer{};
    for (;;)
    {
        const auto size = ::read(source, buffer.data(), buffer.size());
        if (size == 0)
        {
            ::_exit(0);
        }
        if (size < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                pollfd polling{.fd = source, .events = POLLIN, .revents = 0};
                if (::poll(&polling, 1, -1) >= 0 || errno == EINTR)
                {
                    continue;
                }
            }
            ::_exit(1);
        }
        std::size_t offset = 0;
        const auto count = static_cast<std::size_t>(size);
        while (offset < count)
        {
            const auto written = ::write(destination, std::span(buffer).subspan(offset).data(), count - offset);
            if (written < 0 && errno == EINTR)
            {
                continue;
            }
            if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                pollfd polling{.fd = destination, .events = POLLOUT, .revents = 0};
                if (::poll(&polling, 1, -1) >= 0 || errno == EINTR)
                {
                    continue;
                }
            }
            if (written <= 0)
            {
                ::_exit(errno == EPIPE ? 2 : 1);
            }
            offset += static_cast<std::size_t>(written);
        }
    }
}

class ForwardWorker
{
public:
    ForwardWorker() = default;
    ~ForwardWorker()
    {
        if (Pending())
        {
            (void)::kill(Pid, SIGKILL);
            int status = 0;
            while (::waitpid(Pid, &status, 0) < 0 && errno == EINTR)
            {
            }
        }
    }
    ForwardWorker(const ForwardWorker&) = delete;
    ForwardWorker& operator=(const ForwardWorker&) = delete;
    ForwardWorker(ForwardWorker&&) = delete;
    ForwardWorker& operator=(ForwardWorker&&) = delete;

    [[nodiscard]] bool Start(Pipe& pipe, int destination, int unusedRead)
    {
        const int source = pipe.Read.Get();
        Pid = ::fork();
        if (Pid == 0)
        {
            ForwardBytes(source, destination, unusedRead);
        }
        if (Pid < 0)
        {
            return false;
        }
        pipe.Read.Reset();
        return true;
    }

    [[nodiscard]] bool Pending() const noexcept { return Pid > 0 && !Reaped; }
    [[nodiscard]] bool Poll()
    {
        if (!Pending())
        {
            return true;
        }
        int status = 0;
        // wait macros are reexported by the public sys/wait.h header.
        // NOLINTNEXTLINE(misc-include-cleaner)
        const auto waited = ::waitpid(Pid, &status, WNOHANG);
        if (waited < 0)
        {
            return errno == EINTR;
        }
        if (waited == Pid)
        {
            Reaped = true;
            // NOLINTNEXTLINE(misc-include-cleaner)
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            {
                // NOLINTNEXTLINE(misc-include-cleaner)
                errno = WIFEXITED(status) && WEXITSTATUS(status) == 2 ? EPIPE : EIO;
                return false;
            }
        }
        return true;
    }

private:
    // Apple reexports pid_t through the public sys/types.h header.
    // NOLINTNEXTLINE(misc-include-cleaner)
    pid_t Pid{-1};
    bool Reaped{};
};
} // namespace

Result<ProcessResult> RunProcess(const ProcessRequest& request, FailureReport& report)
{
    const auto fail = [&report](std::string_view stage, int code) -> Result<ProcessResult>
    { return std::unexpected(report.Add(ToolError::kInvalidRequest, stage, {}, {code, std::generic_category()})); };
    if (!request.Program.is_absolute() || request.Program.native().contains('\0') ||
        request.Directory.native().contains('\0') || request.Timeout.count() < 0)
    {
        return fail("process_request", EINVAL);
    }
    std::vector<std::string> arguments{request.Program.native()};
    for (const auto& argument : request.Arguments)
    {
        if (argument.contains('\0'))
        {
            return fail("process_argument", EINVAL);
        }
        arguments.push_back(argument);
    }
    std::map<std::string, std::string> environment;
    // Native sentinel-terminated environment; the tool never mutates it.
#ifdef __APPLE__
    const char* const* inherited = *_NSGetEnviron();
#else
    const char* const* inherited = environ;
#endif
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    for (const char* const* cursor = inherited; cursor != nullptr && *cursor != nullptr; ++cursor)
    {
        const std::string entry(*cursor);
        const auto separator = entry.find('=');
        if (separator != std::string::npos)
        {
            environment.insert_or_assign(entry.substr(0, separator), entry.substr(separator + 1));
        }
    }
    for (const auto& [key, value] : request.Environment)
    {
        if (key.empty() || key.contains('=') || key.contains('\0') || value.contains('\0'))
        {
            return fail("process_environment", EINVAL);
        }
        environment.insert_or_assign(key, value);
    }
    std::vector<std::string> environmentStrings;
    environmentStrings.reserve(environment.size());
    for (const auto& [key, value] : environment)
    {
        auto entry = key;
        entry += '=';
        entry += value;
        environmentStrings.push_back(std::move(entry));
    }
    std::vector<char*> argumentPointers;
    argumentPointers.reserve(arguments.size() + 1);
    for (auto& argument : arguments)
    {
        argumentPointers.push_back(argument.data());
    }
    argumentPointers.push_back(nullptr);
    std::vector<char*> environmentPointers;
    environmentPointers.reserve(environmentStrings.size() + 1);
    for (auto& entry : environmentStrings)
    {
        environmentPointers.push_back(entry.data());
    }
    environmentPointers.push_back(nullptr);
    if (request.Cancellation.stop_requested() || (request.InterruptFlag != nullptr && *request.InterruptFlag != 0))
    {
        return fail("process_cancelled", ECANCELED);
    }
    Pipe out;
    Pipe err;
    if (!OpenPipe(out) || !OpenPipe(err))
    {
        return fail("process_pipe", errno);
    }
    SpawnConfiguration spawn;
    int code = ::posix_spawn_file_actions_init(&spawn.Actions);
    if (code != 0)
    {
        return fail("process_actions", code);
    }
    spawn.ActionsInitialized = true;
    code = ::posix_spawnattr_init(&spawn.Attributes);
    if (code != 0)
    {
        return fail("process_attributes", code);
    }
    spawn.AttributesInitialized = true;
    for (const auto& pipe : {&out, &err})
    {
        const int destination = pipe == &out ? STDOUT_FILENO : STDERR_FILENO;
        for (const int action : {
                 ::posix_spawn_file_actions_adddup2(&spawn.Actions, pipe->Write.Get(), destination),
                 ::posix_spawn_file_actions_addclose(&spawn.Actions, pipe->Read.Get()),
                 ::posix_spawn_file_actions_addclose(&spawn.Actions, pipe->Write.Get()),
             })
        {
            if (action != 0)
            {
                return fail("process_actions", action);
            }
        }
    }
    if (!request.Directory.empty())
    {
        code = ::posix_spawn_file_actions_addchdir_np(&spawn.Actions, request.Directory.c_str());
        if (code != 0)
        {
            return fail("process_actions", code);
        }
    }
    for (const int action : {
             ::posix_spawn_file_actions_addopen(&spawn.Actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0),
             ::posix_spawnattr_setflags(&spawn.Attributes, POSIX_SPAWN_SETPGROUP),
             ::posix_spawnattr_setpgroup(&spawn.Attributes, 0),
         })
    {
        if (action != 0)
        {
            return fail("process_actions", action);
        }
    }
    pid_t pid = 0;
    code = ::posix_spawn(&pid, request.Program.c_str(), &spawn.Actions, &spawn.Attributes, argumentPointers.data(),
                         environmentPointers.data());
    if (code != 0)
    {
        return fail("process_start", code);
    }
    Child child(pid);
    out.Write.Reset();
    err.Write.Reset();
    ForwardWorker outForward;
    ForwardWorker errForward;
    if (request.OutputMode == ProcessOutputMode::kForward && (!outForward.Start(out, STDOUT_FILENO, err.Read.Get()) ||
                                                              !errForward.Start(err, STDERR_FILENO, out.Read.Get())))
    {
        return fail("process_forward_start", errno);
    }
    ProcessResult result;
    const auto started = std::chrono::steady_clock::now();
    int status = 0;
    while (!child.Reaped || out.Read.Get() >= 0 || err.Read.Get() >= 0 || outForward.Pending() || errForward.Pending())
    {
        if (!outForward.Poll() || !errForward.Poll() || !Drain(out, result.Stdout, request) ||
            !Drain(err, result.Stderr, request))
        {
            return fail("process_output", errno);
        }
        if (request.Cancellation.stop_requested() || (request.InterruptFlag != nullptr && *request.InterruptFlag != 0))
        {
            return fail("process_cancelled", ECANCELED);
        }
        if (request.Timeout.count() != 0 && std::chrono::steady_clock::now() - started >= request.Timeout)
        {
            return fail("process_timeout", ETIMEDOUT);
        }
        if (!child.Reaped)
        {
            // Linux exposes wait macros through implementation headers of sys/wait.h.
            // NOLINTNEXTLINE(misc-include-cleaner)
            const auto waited = ::waitpid(pid, &status, WNOHANG);
            if (waited < 0 && errno != EINTR)
            {
                return fail("process_wait", errno);
            }
            if (waited == pid)
            {
                child.Reaped = true;
                (void)::kill(-pid, SIGKILL);
            }
        }
        std::array<pollfd, 2> polling{
            {
                {
                    .fd = out.Read.Get(),
                    .events = POLLIN,
                    .revents = 0,
                },
                {
                    .fd = err.Read.Get(),
                    .events = POLLIN,
                    .revents = 0,
                },
            },
        };
        if (::poll(polling.data(), polling.size(), 5) < 0 && errno != EINTR)
        {
            return fail("process_poll", errno);
        }
    }
    // Linux exposes wait macros through implementation headers of sys/wait.h.
    // NOLINTNEXTLINE(misc-include-cleaner)
    result.ExitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    return result;
}
} // namespace pg::abi
