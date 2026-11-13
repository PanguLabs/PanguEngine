#include "NativeModuleLibrary.h"
#include "FailureContext.h"

#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <Result.h>

#include <cstddef>
#include <dlfcn.h>

#include <expected>
#include <filesystem>
#include <string_view>

namespace pg::module::detail
{
namespace
{
std::string_view LoaderMessage(const char* text) noexcept
{
    std::size_t size{};
    // Loader 文本只同步借用；最多读取 1024 字节，随后由 Context 立即复制。
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    while (size < 1024 && text[size] != 0)
    {
        ++size;
    }
    return {text, size};
}
} // namespace
Result<NativeModuleLibrary> NativeModuleLibrary::Open(const std::filesystem::path& path,
                                                      FailureContext* failure) noexcept
{
    if (!path.is_absolute())
    {
        return std::unexpected(MakeModuleError(ModuleError::kInvalidPath));
    }
    dlerror();
    // 受控 C++ ABI 需要跨独立装载组合并公开 RTTI；私有符号仍由目标可见性隐藏。
    auto* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (handle == nullptr)
    {
        // dlerror 文本由 Loader 借出；立即消费，不让错误值借用可卸载状态。
        const auto* text = dlerror();
        const auto error = MakeModuleError(ModuleError::kLibraryOpenFailed);
        if (failure != nullptr)
        {
            failure->SetPrimary(error, "dlopen failed");
            if (text != nullptr)
            {
                failure->AddContext("loader", LoaderMessage(text));
            }
        }
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
    dlerror();
    auto* symbol = dlsym(Handle, "PanguModuleQueryV1");
    const auto* text = dlerror();
    if (text != nullptr || symbol == nullptr)
    {
        const auto error = MakeModuleError(ModuleError::kQuerySymbolMissing);
        if (failure != nullptr)
        {
            failure->SetPrimary(error, "dlsym failed");
            if (text != nullptr)
            {
                failure->AddContext("loader", LoaderMessage(text));
            }
        }
        AddPath(failure, Path);
        return std::unexpected(error);
    }
    // POSIX 允许 dlsym 的地址转换为函数指针；仅此平台接缝承担该约定。
    return reinterpret_cast<QueryModuleFn>(symbol); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}
Status NativeModuleLibrary::CloseNative(FailureContext* failure) noexcept
{
    dlerror();
    if (dlclose(Handle) != 0)
    {
        const auto* text = dlerror();
        const auto error = MakeModuleError(ModuleError::kLibraryCloseFailed);
        if (failure != nullptr)
        {
            failure->SetPrimary(error, "dlclose failed");
            if (text != nullptr)
            {
                failure->AddContext("loader", LoaderMessage(text));
            }
        }
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
    const std::string_view native(path.native());
    failure->AddContext("path", native.substr(0, 1024));
    if (native.size() > 1024)
    {
        failure->AddContext("path.truncated", "true");
    }
}
} // namespace pg::module::detail
