#include "Driver/Diagnostic.h"
#include "Output/PublishInternal.h"
#define NOMINMAX
#include <Windows.h> // IWYU pragma: keep
#include <errhandlingapi.h>
#include <fileapi.h>
#include <handleapi.h>
#include <minwindef.h>
#include <winbase.h>
#include <winerror.h>
#include <winnt.h>
#undef ReplaceFile
#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace pg::meta::tool
{
ToolResult<void> WriteExclusive(const std::filesystem::path& path, std::string_view content)
{
    auto* const handle =
        CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        const auto failure = GetLastError();
        return std::unexpected(
            Diagnostics{{.Code = "publish_failed", .Message = "创建 staging 候选失败: " + std::to_string(failure)}});
    }
    DWORD failure = ERROR_SUCCESS;
    while (!content.empty())
    {
        DWORD written = 0;
        const auto count = static_cast<DWORD>(std::min(content.size(), std::size_t{1048576}));
        if (WriteFile(handle, content.data(), count, &written, nullptr) == 0)
        {
            failure = GetLastError();
            break;
        }
        if (written == 0)
        {
            failure = ERROR_WRITE_FAULT;
            break;
        }
        content.remove_prefix(written);
    }
    if (CloseHandle(handle) == 0 && failure == ERROR_SUCCESS)
    {
        failure = GetLastError();
    }
    if (failure != ERROR_SUCCESS)
    {
        // CREATE_NEW 已取得本文件所有权；清理不能覆盖首次写入/关闭错误。
        DeleteFileW(path.c_str());
        return std::unexpected(
            Diagnostics{{.Code = "publish_failed", .Message = "写入 staging 候选失败: " + std::to_string(failure)}});
    }
    return {};
}
bool IsPlatformOutputPath(const std::filesystem::path& relative)
{
    // Win32 会裁剪尾点/空格并解释 DOS 设备名；这些不是普通输出槽位。
    // 此处仅识别平台保留拼写，Unicode 文件身份仍由实际 NTFS 探测判断。
    for (const auto& part : relative)
    {
        auto text = part.native();
        if (text.empty() || text.back() == L'.' || text.back() == L' ')
        {
            return false;
        }
        text.resize(text.contains(L'.') ? text.find(L'.') : text.size());
        for (auto& ch : text)
        {
            if (ch >= L'a' && ch <= L'z')
            {
                ch = static_cast<wchar_t>(ch - (L'a' - L'A'));
            }
        }
        if (text == L"CON" || text == L"PRN" || text == L"AUX" || text == L"NUL" || text == L"CONIN$" ||
            text == L"CONOUT$")
        {
            return false;
        }
        if (text.size() == 4 && (text.starts_with(L"COM") || text.starts_with(L"LPT")) &&
            ((text.back() >= L'0' && text.back() <= L'9') || text.back() == L'\u00b9' || text.back() == L'\u00b2' ||
             text.back() == L'\u00b3'))
        {
            return false;
        }
    }
    return true;
}
ToolResult<void> ReplaceFile(const std::filesystem::path& staged, const std::filesystem::path& destination)
{
    // 同卷 rename 替换单文件；禁止 COPY_ALLOWED，不能降级为跨卷复制。
    if (MoveFileExW(staged.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING) == 0)
    {
        return std::unexpected(
            Diagnostics{{.Code = "publish_failed", .Message = "原子替换失败: " + std::to_string(GetLastError())}});
    }
    return {};
}
} // namespace pg::meta::tool
