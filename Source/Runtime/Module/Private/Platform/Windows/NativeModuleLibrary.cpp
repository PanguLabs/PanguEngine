#include "NativeModuleLibrary.h"
#include "FailureContext.h"

#include <Error.h>
#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <Result.h>

#include <Windows.h> // IWYU pragma: keep
#include <cstddef>
#include <errhandlingapi.h>
#include <libloaderapi.h>
#include <minwindef.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <source_location>
#include <string_view>
#include <stringapiset.h>
#include <winnls.h>

namespace pg::module::detail
{
namespace
{
Error NativeError(ModuleError code, DWORD value, FailureContext* failure,
                  std::source_location location = std::source_location::current()) noexcept
{
    constexpr NativeErrorSourceId kWin32Error{0x77696e3332ULL};
    const Error error{kModuleErrorDomain, static_cast<std::uint32_t>(code),
                      NativeErrorCode{.Source = kWin32Error, .Value = static_cast<std::int64_t>(value)}};
    if (failure != nullptr)
    {
        failure->SetPrimary(error, "Win32 module loader failed", location);
    }
    return error;
}
} // namespace
Result<NativeModuleLibrary> NativeModuleLibrary::Open(const std::filesystem::path& path,
                                                      FailureContext* failure) noexcept
{
    if (!path.is_absolute())
    {
        return std::unexpected(MakeModuleError(ModuleError::kInvalidPath));
    }
    // 每次装载限定搜索作用域，不修改进程 DLL 搜索目录或环境。
    auto* const handle =
        LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (handle == nullptr)
    {
        const auto value = GetLastError();
        const auto error = NativeError(ModuleError::kLibraryOpenFailed, value, failure);
        AddPath(failure, path);
        return std::unexpected(error);
    }
    return NativeModuleLibrary(handle, path);
}
Result<QueryModuleFn> NativeModuleLibrary::ResolveQuery(FailureContext* failure) const noexcept
{
    if (Handle == nullptr)
    {
        return std::unexpected(MakeModuleError(ModuleError::kInvalidState));
    }
    const auto symbol = GetProcAddress(static_cast<HMODULE>(Handle), "PanguModuleQueryV1");
    if (symbol == nullptr)
    {
        const auto value = GetLastError();
        const auto error = NativeError(ModuleError::kQuerySymbolMissing, value, failure);
        AddPath(failure, Path);
        return std::unexpected(error);
    }
    // Windows 原生查询返回通用函数指针，签名转换仅封装在此平台接缝。
    return reinterpret_cast<QueryModuleFn>(symbol); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}
Status NativeModuleLibrary::CloseNative(FailureContext* failure) noexcept
{
    if (FreeLibrary(static_cast<HMODULE>(Handle)) == 0)
    {
        const auto value = GetLastError();
        const auto error = NativeError(ModuleError::kLibraryCloseFailed, value, failure);
        AddPath(failure, Path);
        return std::unexpected(error);
    }
    return {};
}
void NativeModuleLibrary::AddPath(FailureContext* failure, const std::filesystem::path& path) noexcept
{
    if (failure == nullptr)
    {
        return;
    }
    std::array<char, 4096> text{};
    const auto count = std::min<std::size_t>(path.native().size(), 1024);
    const auto length = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), static_cast<int>(count), text.data(),
                                            static_cast<int>(text.size()), nullptr, nullptr);
    if (length > 0)
    {
        failure->AddContext("path", std::string_view(text.data(), static_cast<std::size_t>(length)));
    }
    if (count != path.native().size())
    {
        failure->AddContext("path.truncated", "true");
    }
}
} // namespace pg::module::detail
