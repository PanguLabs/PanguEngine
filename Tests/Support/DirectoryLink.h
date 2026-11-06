#pragma once

#include <filesystem>
#include <system_error>

#ifdef _WIN32
#include <array>
#include <cerrno>
#include <process.h>
#include <string>
#endif

namespace pg::test
{
// Windows 用无需符号链接权限的 Junction；其它宿主使用真实 directory symlink。
[[nodiscard]] inline std::error_code CreateDirectoryLink(const std::filesystem::path& target,
                                                         const std::filesystem::path& link)
{
#ifdef _WIN32
    const auto quote = [](const std::filesystem::path& path)
    {
        std::wstring escaped = L"'";
        for (const auto character : path.native())
        {
            escaped += character;
            if (character == L'\'')
            {
                escaped += L'\'';
            }
        }
        return escaped + L"'";
    };
    const auto command = L"\"New-Item -ItemType Junction -Path " + quote(link) + L" -Target " + quote(target) +
                         L" -ErrorAction Stop | Out-Null\"";
    const std::array arguments{
        L"powershell.exe", L"-NoProfile",   L"-NonInteractive",
        L"-Command",       command.c_str(), static_cast<const wchar_t*>(nullptr),
    };
    const auto result = _wspawnvp(_P_WAIT, L"powershell.exe", arguments.data());
    if (result != 0)
    {
        return result == -1 ? std::error_code(errno, std::generic_category())
                            : std::make_error_code(std::errc::io_error);
    }
    return {};
#else
    std::error_code error;
    std::filesystem::create_directory_symlink(target, link, error);
    return error;
#endif
}
} // namespace pg::test
