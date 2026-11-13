#include "NativeLogIo.h"
#include "Error.h"
#include "LogRecord.h"
#include "LogTypes.h"
#include "Result.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h> // IWYU pragma: keep
#include <array>
#include <consoleapi.h>
#include <cstdint>
#include <errhandlingapi.h>
#include <expected>
#include <fileapi.h>
#include <handleapi.h>
#include <minwindef.h>
#include <optional>
#include <processenv.h>
#include <processthreadsapi.h>
#include <span>
#include <string_view>
#include <stringapiset.h>
#include <utility>
#include <winbase.h>
#include <winerror.h>
#include <winnls.h>
#include <winnt.h>

namespace pg::log::detail
{
namespace
{
// 仅在 Win32 边界转换不透明 HANDLE。
HANDLE HandleValue(NativeHandle handle) noexcept
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr) Win32 不透明句柄。
    return reinterpret_cast<HANDLE>(
        handle.Value); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
}
} // namespace
Error NativeIoError(std::int64_t code, LogError kind) noexcept
{
    return {kLogErrorDomain,
            static_cast<std::uint32_t>(kind),
            {.Source = NativeErrorSourceId{0x77696e3332ULL}, .Value = code}};
}
Result<NativeHandle> OpenConsoleNative() noexcept
{
    HANDLE duplicate{};
    if (DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_ERROR_HANDLE), GetCurrentProcess(), &duplicate, 0, FALSE,
                        DUPLICATE_SAME_ACCESS) == 0)
    {
        return std::unexpected(NativeIoError(GetLastError(), LogError::kFileOpenFailed));
    }
    return NativeHandle{
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) 私有原生句柄存储。
        reinterpret_cast<std::intptr_t>(duplicate),
    }; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
}
OutputOutcome WriteNative(NativeHandle handle, std::string_view text, bool console) noexcept
{
    if (text.empty())
    {
        return {.Complete = true};
    }
    DWORD mode{};
    DWORD written{};
    if (console && GetConsoleMode(HandleValue(handle), &mode) != 0)
    {
        std::array<wchar_t, kEncodedLogLineCapacity> wide{};
        const int units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                              wide.data(), static_cast<int>(wide.size()));
        if (units == 0)
        {
            return {.Failure = NativeIoError(GetLastError())};
        }
        const auto success =
            WriteConsoleW(HandleValue(handle), wide.data(), static_cast<DWORD>(units), &written, nullptr);
        const auto error = success == 0 ? std::optional(NativeIoError(GetLastError())) : std::nullopt;
        // 原生进度保留 UTF-16 单位；逻辑进度只计算完整 Unicode 字符对应的 UTF-8 字节。
        auto completeUnits = written;
        if (completeUnits != 0 && std::span(wide).first(completeUnits).back() >= 0xd800 &&
            std::span(wide).first(completeUnits).back() <= 0xdbff)
        {
            --completeUnits;
        }
        const int bytes = completeUnits == 0
                              ? 0
                              : WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(completeUnits), nullptr,
                                                    0, nullptr, nullptr);
        return {
            .BytesWritten = static_cast<std::uint64_t>(bytes),
            .Failure = error,
            .Complete = success != 0 && std::cmp_equal(written, units),
            .NativeUnitsWritten = written,
        };
    }
    const auto success =
        WriteFile(HandleValue(handle), text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    return {
        .BytesWritten = written,
        .Failure = success == 0 ? std::optional(NativeIoError(GetLastError())) : std::nullopt,
        .Complete = success != 0 && written == text.size(),
        .NativeUnitsWritten = written,
    };
}
Status FlushNative(NativeHandle handle, bool console) noexcept
{
    // 终端/重定向直接写 OS，无用户态缓冲；文件额外请求 FlushFileBuffers，不承诺设备持久化。
    if (console)
    {
        return {};
    }
    if (FlushFileBuffers(HandleValue(handle)) == 0)
    {
        return std::unexpected(NativeIoError(GetLastError()));
    }
    return {};
}
Status CloseNative(NativeHandle& handle) noexcept
{
    if (!handle.Valid())
    {
        return {};
    }
    if (CloseHandle(HandleValue(handle)) != 0)
    {
        handle = {};
        return {};
    }
    const auto code = GetLastError();
    if (code == ERROR_INVALID_HANDLE)
    {
        handle = {};
    }
    return std::unexpected(NativeIoError(code));
}
} // namespace pg::log::detail
