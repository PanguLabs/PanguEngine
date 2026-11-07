#include "NativeModuleLibrary.h"

#include <Error.h>
#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <Result.h>

#include <Windows.h> // IWYU pragma: keep
#include <errhandlingapi.h>
#include <libloaderapi.h>
#include <minwindef.h>

#include <cstdint>
#include <expected>
#include <filesystem>

namespace pg::module::detail
{
namespace
{
Error NativeError(ModuleError code) noexcept
{
    constexpr NativeErrorSourceId kWin32Error{0x77696e3332ULL};
    return {kModuleErrorDomain, static_cast<std::uint32_t>(code),
            NativeErrorCode{.Source = kWin32Error, .Value = static_cast<std::int64_t>(GetLastError())}};
}
} // namespace
Result<NativeModuleLibrary> NativeModuleLibrary::Open(const std::filesystem::path& path) noexcept
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
        return std::unexpected(NativeError(ModuleError::kLibraryOpenFailed));
    }
    return NativeModuleLibrary(handle);
}
Result<QueryModuleFn> NativeModuleLibrary::ResolveQuery() const noexcept
{
    if (Handle == nullptr)
    {
        return std::unexpected(MakeModuleError(ModuleError::kInvalidState));
    }
    const auto symbol = GetProcAddress(static_cast<HMODULE>(Handle), "PanguModuleQueryV1");
    if (symbol == nullptr)
    {
        return std::unexpected(NativeError(ModuleError::kQuerySymbolMissing));
    }
    // Windows 原生查询返回通用函数指针，签名转换仅封装在此平台接缝。
    return reinterpret_cast<QueryModuleFn>(symbol); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}
Status NativeModuleLibrary::CloseNative() noexcept
{
    if (FreeLibrary(static_cast<HMODULE>(Handle)) == 0)
    {
        return std::unexpected(NativeError(ModuleError::kLibraryCloseFailed));
    }
    return {};
}
} // namespace pg::module::detail
