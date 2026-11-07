#include "NativeModuleLibrary.h"

#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <Result.h>

#include <dlfcn.h>

#include <expected>
#include <filesystem>

namespace pg::module::detail
{
Result<NativeModuleLibrary> NativeModuleLibrary::Open(const std::filesystem::path& path) noexcept
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
        dlerror();
        return std::unexpected(MakeModuleError(ModuleError::kLibraryOpenFailed));
    }
    return NativeModuleLibrary(handle);
}
Result<QueryModuleFn> NativeModuleLibrary::ResolveQuery() const noexcept
{
    if (Handle == nullptr)
    {
        return std::unexpected(MakeModuleError(ModuleError::kInvalidState));
    }
    dlerror();
    auto* symbol = dlsym(Handle, "PanguModuleQueryV1");
    if (dlerror() != nullptr || symbol == nullptr)
    {
        return std::unexpected(MakeModuleError(ModuleError::kQuerySymbolMissing));
    }
    // POSIX 允许 dlsym 的地址转换为函数指针；仅此平台接缝承担该约定。
    return reinterpret_cast<QueryModuleFn>(symbol); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}
Status NativeModuleLibrary::CloseNative() noexcept
{
    dlerror();
    if (dlclose(Handle) != 0)
    {
        dlerror();
        return std::unexpected(MakeModuleError(ModuleError::kLibraryCloseFailed));
    }
    return {};
}
} // namespace pg::module::detail
