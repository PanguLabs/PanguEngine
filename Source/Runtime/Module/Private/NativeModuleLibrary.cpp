#include "NativeModuleLibrary.h"
#include "FailureContext.h"
#include <Check.h>

#include <ModuleError.h>
#include <Result.h>

#include <expected>
#include <filesystem>
#include <utility>

namespace pg::module::detail
{
NativeModuleLibrary::NativeModuleLibrary(void* handle, std::filesystem::path path) noexcept
    : Path(std::move(path))
    , Handle(handle)
{
}
NativeModuleLibrary::~NativeModuleLibrary()
{
    if (!Release())
    {
        // 失败仍拥有原生持有；不能丢失清理责任并继续运行。
        pg::Fatal("native module ownership lost during destruction");
    }
}
NativeModuleLibrary::NativeModuleLibrary(NativeModuleLibrary&& other) noexcept
    : Path(std::move(other.Path))
    , Handle(std::exchange(other.Handle, nullptr))
    , FailNextClose(std::exchange(other.FailNextClose, false))
{
}
NativeModuleLibrary& NativeModuleLibrary::operator=(NativeModuleLibrary&& other) noexcept
{
    if (this != &other)
    {
        if (!Release())
        {
            pg::Fatal("native module ownership lost during destruction");
        }
        Path = std::move(other.Path);
        Handle = std::exchange(other.Handle, nullptr);
        FailNextClose = std::exchange(other.FailNextClose, false);
    }
    return *this;
}
Status NativeModuleLibrary::Release(FailureContext* failure) noexcept
{
    if (Handle == nullptr)
    {
        return {};
    }
    if (std::exchange(FailNextClose, false))
    {
        const auto error = MakeModuleError(ModuleError::kLibraryCloseFailed);
        if (failure != nullptr)
        {
            failure->SetPrimary(error, "injected native close failure");
            AddPath(failure, Path);
        }
        return std::unexpected(error);
    }
    const auto closed = CloseNative(failure);
    if (closed)
    {
        Handle = nullptr;
    }
    return closed;
}
} // namespace pg::module::detail
