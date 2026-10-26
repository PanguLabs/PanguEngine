#include "Process.h"
#include "ToolReport.h"
#include <Result.h>
#include <map>
#include <string>

#define NOMINMAX
#include <Windows.h> // IWYU pragma: keep - supplies SDK prerequisites for the native process API.
#include <basetsd.h>
#include <errhandlingapi.h>
#include <fileapi.h>
#include <handleapi.h>
#include <ioapiset.h>
#include <jobapi2.h>
#include <minwinbase.h>
#include <minwindef.h>
#include <namedpipeapi.h>
#include <processenv.h>
#include <processthreadsapi.h>
#include <stringapiset.h>
#include <synchapi.h>
#include <winbase.h>
#include <winerror.h>
#include <winnls.h>
#include <winnt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cwchar>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace pg::abi
{
namespace
{
struct CloseHandle
{
    void operator()(void* handle) const noexcept { (void)::CloseHandle(handle); }
};
using Handle = std::unique_ptr<void, CloseHandle>;

[[nodiscard]] std::wstring Wide(std::string_view text)
{
    const int size =
        ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size == 0)
    {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    (void)::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                result.data(), size);
    return result;
}

// Microsoft CRT argv rules; no command interpreter participates.
[[nodiscard]] std::wstring Quote(const std::wstring& value)
{
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto character : value)
    {
        if (character == L'\\')
        {
            ++slashes;
            continue;
        }
        result.append(slashes * (character == L'"' ? 2 : 1), L'\\');
        slashes = 0;
        if (character == L'"')
        {
            result += L'\\';
        }
        result += character;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}

struct EnvironmentOrder
{
    bool operator()(const std::wstring& left, const std::wstring& right) const
    {
        return ::CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                                      static_cast<int>(right.size()), TRUE) == CSTR_LESS_THAN;
    }
};

[[nodiscard]] std::vector<wchar_t> EnvironmentBlock(const ProcessRequest& request)
{
    std::map<std::wstring, std::wstring, EnvironmentOrder> entries;
    wchar_t* inherited = ::GetEnvironmentStringsW();
    if (inherited == nullptr)
    {
        return {};
    }
    // Windows returns a native double-NUL block without an explicit size.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    for (const wchar_t* cursor = inherited; *cursor != L'\0'; cursor += std::wcslen(cursor) + 1)
    {
        const std::wstring entry(cursor);
        const auto separator = entry.find(L'=', 1);
        if (separator != std::wstring::npos)
        {
            entries.insert_or_assign(entry.substr(0, separator), entry.substr(separator + 1));
        }
    }
    (void)::FreeEnvironmentStringsW(inherited);
    for (const auto& [key, value] : request.Environment)
    {
        entries.insert_or_assign(Wide(key), Wide(value));
    }
    std::vector<wchar_t> result;
    for (const auto& [key, value] : entries)
    {
        auto entry = key;
        entry += L'=';
        entry += value;
        result.insert(result.end(), entry.begin(), entry.end());
        result.push_back(L'\0');
    }
    result.push_back(L'\0');
    if (entries.empty())
    {
        result.push_back(L'\0');
    }
    return result;
}

struct Pipe
{
    Handle Read;
    Handle Write;
};

[[nodiscard]] bool OpenPipe(Pipe& pipe)
{
    SECURITY_ATTRIBUTES security{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = nullptr,
        .bInheritHandle = TRUE,
    };
    HANDLE read = nullptr;
    HANDLE write = nullptr;
    if (::CreatePipe(&read, &write, &security, 0) == 0)
    {
        return false;
    }
    pipe.Read.reset(read);
    pipe.Write.reset(write);
    return ::SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0) != 0;
}

// One fixed-size outstanding block per stream. Only this writer thread can
// block on the downstream handle; the supervising loop always remains runnable.
class ForwardWriter
{
public:
    explicit ForwardWriter(HANDLE destination)
        : Destination(destination)
        , Writer([this] { Run(); })
    {
    }
    ~ForwardWriter()
    {
        {
            const std::scoped_lock lock(Mutex);
            Stopping = true;
        }
        Available.notify_one();
        // Cancellation can race immediately before the next WriteFile starts.
        // Keep cancelling until the thread actually exits; never cancel once
        // and then join a possibly newly blocked synchronous write.
        while (::WaitForSingleObject(Writer.native_handle(), 5) == WAIT_TIMEOUT)
        {
            (void)::CancelSynchronousIo(Writer.native_handle());
        }
        Writer.join();
    }
    ForwardWriter(const ForwardWriter&) = delete;
    ForwardWriter& operator=(const ForwardWriter&) = delete;
    ForwardWriter(ForwardWriter&&) = delete;
    ForwardWriter& operator=(ForwardWriter&&) = delete;

    [[nodiscard]] bool Pending()
    {
        const std::scoped_lock lock(Mutex);
        return HasBlock;
    }
    [[nodiscard]] DWORD Error()
    {
        const std::scoped_lock lock(Mutex);
        return ErrorCode;
    }
    void Submit(std::span<const char> bytes)
    {
        {
            const std::scoped_lock lock(Mutex);
            std::ranges::copy(bytes, Buffer.begin());
            Size = static_cast<DWORD>(bytes.size());
            HasBlock = true;
        }
        Available.notify_one();
    }

private:
    void Run()
    {
        for (;;)
        {
            std::unique_lock lock(Mutex);
            Available.wait(lock, [this] { return Stopping || HasBlock; });
            if (Stopping)
            {
                return;
            }
            const DWORD size = Size;
            lock.unlock();
            DWORD offset = 0;
            DWORD error = 0;
            while (offset < size)
            {
                {
                    const std::scoped_lock stateLock(Mutex);
                    if (Stopping)
                    {
                        return;
                    }
                }
                DWORD written = 0;
                if (::WriteFile(Destination, std::span(Buffer).subspan(offset).data(), size - offset, &written,
                                nullptr) == 0)
                {
                    error = ::GetLastError();
                    break;
                }
                if (written == 0)
                {
                    error = ERROR_WRITE_FAULT;
                    break;
                }
                offset += written;
            }
            lock.lock();
            ErrorCode = error;
            HasBlock = false;
            if (error != 0)
            {
                return;
            }
        }
    }

    HANDLE Destination;
    std::mutex Mutex;
    std::condition_variable Available;
    std::array<char, 8192> Buffer{};
    DWORD Size{};
    DWORD ErrorCode{};
    bool HasBlock{};
    bool Stopping{};
    std::thread Writer;
};

[[nodiscard]] bool Drain(Pipe& pipe, std::string& output, ForwardWriter* forward, const ProcessRequest& request)
{
    std::array<char, 8192> buffer{};
    if (forward != nullptr)
    {
        if (const DWORD error = forward->Error(); error != 0)
        {
            ::SetLastError(error);
            return false;
        }
        if (forward->Pending())
        {
            return true;
        }
    }
    for (int batch = 0; batch < 8 && pipe.Read; ++batch)
    {
        DWORD available = 0;
        if (::PeekNamedPipe(pipe.Read.get(), nullptr, 0, nullptr, &available, nullptr) == 0)
        {
            if (::GetLastError() != ERROR_BROKEN_PIPE)
            {
                return false;
            }
            pipe.Read.reset();
            break;
        }
        if (available == 0)
        {
            break;
        }
        DWORD size = 0;
        if (::ReadFile(pipe.Read.get(), buffer.data(), std::min(available, static_cast<DWORD>(buffer.size())), &size,
                       nullptr) == 0)
        {
            return false;
        }
        if (forward != nullptr)
        {
            forward->Submit(std::span(buffer).first(size));
            break;
        }
        if (size > request.CaptureLimitBytes - output.size())
        {
            ::SetLastError(ERROR_BUFFER_OVERFLOW);
            return false;
        }
        output.append(buffer.data(), size);
    }
    return true;
}
} // namespace

Result<ProcessResult> RunProcess(const ProcessRequest& request, FailureReport& report)
{
    const auto fail = [&report](std::string_view stage) -> Result<ProcessResult>
    {
        return std::unexpected(report.Add(ToolError::kInvalidRequest, stage, {},
                                          {static_cast<int>(::GetLastError()), std::system_category()}));
    };
    if (!request.Program.is_absolute() || request.Program.native().contains(L'\0') ||
        request.Directory.native().contains(L'\0') || request.Timeout.count() < 0)
    {
        ::SetLastError(ERROR_INVALID_PARAMETER);
        return fail("process_request");
    }
    std::wstring command = Quote(request.Program.native());
    for (const auto& argument : request.Arguments)
    {
        const auto wide = Wide(argument);
        if (argument.contains('\0') || (!argument.empty() && wide.empty()))
        {
            ::SetLastError(ERROR_INVALID_PARAMETER);
            return fail("process_argument");
        }
        command += L' ' + Quote(wide);
    }
    for (const auto& [key, value] : request.Environment)
    {
        if (key.empty() || key.contains('=') || key.contains('\0') || value.contains('\0') || Wide(key).empty() ||
            (!value.empty() && Wide(value).empty()))
        {
            ::SetLastError(ERROR_INVALID_PARAMETER);
            return fail("process_environment");
        }
    }
    if (request.Cancellation.stop_requested() || (request.InterruptFlag != nullptr && *request.InterruptFlag != 0))
    {
        ::SetLastError(ERROR_CANCELLED);
        return fail("process_cancelled");
    }
    auto environment = EnvironmentBlock(request);
    if (environment.empty())
    {
        return fail("process_environment");
    }
    Pipe out;
    Pipe err;
    if (!OpenPipe(out) || !OpenPipe(err))
    {
        return fail("process_pipe");
    }
    const Handle job(::CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || ::SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) == 0)
    {
        return fail("process_job");
    }
    SECURITY_ATTRIBUTES security{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = nullptr,
        .bInheritHandle = TRUE,
    };
    Handle input(
        ::CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
    if (input.get() == INVALID_HANDLE_VALUE)
    {
        const auto* const invalid = input.release();
        (void)invalid;
        return fail("process_stdin");
    }
    SIZE_T attributeBytes = 0;
    (void)::InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<std::max_align_t> storage((attributeBytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
    // OS opaque structure requires aligned caller-owned storage.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (::InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes) == 0)
    {
        return fail("process_attributes");
    }
    const auto deleteAttributes = [](LPPROC_THREAD_ATTRIBUTE_LIST value) { ::DeleteProcThreadAttributeList(value); };
    const std::unique_ptr<_PROC_THREAD_ATTRIBUTE_LIST, decltype(deleteAttributes)> attributeOwner(attributes);
    std::array<HANDLE, 3> inherited{input.get(), out.Write.get(), err.Write.get()};
    if (::UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                    static_cast<void*>(inherited.data()), sizeof(inherited), nullptr, nullptr) == 0)
    {
        return fail("process_attributes");
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.get();
    startup.StartupInfo.hStdOutput = out.Write.get();
    startup.StartupInfo.hStdError = err.Write.get();
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION info{};
    if (::CreateProcessW(request.Program.c_str(), command.data(), nullptr, nullptr, TRUE,
                         CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
                         environment.data(), request.Directory.empty() ? nullptr : request.Directory.c_str(),
                         &startup.StartupInfo, &info) == 0)
    {
        return fail("process_start");
    }
    const Handle process(info.hProcess);
    const Handle thread(info.hThread);
    if (::AssignProcessToJobObject(job.get(), process.get()) == 0 || ::ResumeThread(thread.get()) == MAXDWORD)
    {
        const auto error = ::GetLastError();
        (void)::TerminateProcess(process.get(), 1);
        (void)::WaitForSingleObject(process.get(), INFINITE);
        ::SetLastError(error);
        return fail("process_start");
    }
    out.Write.reset();
    err.Write.reset();
    std::optional<ForwardWriter> outForward;
    std::optional<ForwardWriter> errForward;
    if (request.OutputMode == ProcessOutputMode::kForward)
    {
        outForward.emplace(::GetStdHandle(STD_OUTPUT_HANDLE));
        errForward.emplace(::GetStdHandle(STD_ERROR_HANDLE));
    }
    ProcessResult result;
    const auto started = std::chrono::steady_clock::now();
    bool exited = false;
    std::string_view failure;
    DWORD error = 0;
    while (!exited || out.Read || err.Read || (outForward && (outForward->Pending() || outForward->Error() != 0)) ||
           (errForward && (errForward->Pending() || errForward->Error() != 0)))
    {
        if (!Drain(out, result.Stdout, outForward ? &*outForward : nullptr, request) ||
            !Drain(err, result.Stderr, errForward ? &*errForward : nullptr, request))
        {
            failure = "process_output";
            error = ::GetLastError();
            break;
        }
        if (request.Cancellation.stop_requested() || (request.InterruptFlag != nullptr && *request.InterruptFlag != 0))
        {
            failure = "process_cancelled";
            error = ERROR_CANCELLED;
            break;
        }
        if (request.Timeout.count() != 0 && std::chrono::steady_clock::now() - started >= request.Timeout)
        {
            failure = "process_timeout";
            error = ERROR_TIMEOUT;
            break;
        }
        const auto waited = ::WaitForSingleObject(process.get(), 5);
        if (waited == WAIT_FAILED)
        {
            failure = "process_wait";
            error = ::GetLastError();
            break;
        }
        if (!exited && waited == WAIT_OBJECT_0)
        {
            exited = true;
            (void)::TerminateJobObject(job.get(), 1);
        }
    }
    if (!failure.empty())
    {
        (void)::TerminateJobObject(job.get(), 1);
        (void)::WaitForSingleObject(process.get(), INFINITE);
        ::SetLastError(error);
        return fail(failure);
    }
    DWORD exitCode = 0;
    if (::GetExitCodeProcess(process.get(), &exitCode) == 0)
    {
        return fail("process_exit_code");
    }
    result.ExitCode = static_cast<int>(exitCode);
    return result;
}
} // namespace pg::abi
