#include "NativeModuleLibrary.h"

#include <ModuleError.h>
#include <Result.h>

#include <exception>
#include <expected>
#include <utility>

namespace pg::module::detail
{
NativeModuleLibrary::NativeModuleLibrary(void* handle) noexcept
    : Handle(handle)
{
}
NativeModuleLibrary::~NativeModuleLibrary()
{
    if (!Release())
    {
        // 失败仍拥有原生持有；不能丢失清理责任并继续运行。
        std::terminate();
    }
}
NativeModuleLibrary::NativeModuleLibrary(NativeModuleLibrary&& other) noexcept
    : Handle(std::exchange(other.Handle, nullptr))
    , FailNextClose(std::exchange(other.FailNextClose, false))
{
}
NativeModuleLibrary& NativeModuleLibrary::operator=(NativeModuleLibrary&& other) noexcept
{
    if (this != &other)
    {
        if (!Release())
        {
            std::terminate();
        }
        Handle = std::exchange(other.Handle, nullptr);
        FailNextClose = std::exchange(other.FailNextClose, false);
    }
    return *this;
}
Status NativeModuleLibrary::Release() noexcept
{
    if (Handle == nullptr)
    {
        return {};
    }
    if (std::exchange(FailNextClose, false))
    {
        return std::unexpected(MakeModuleError(ModuleError::kLibraryCloseFailed));
    }
    const auto closed = CloseNative();
    if (closed)
    {
        Handle = nullptr;
    }
    return closed;
}
} // namespace pg::module::detail
