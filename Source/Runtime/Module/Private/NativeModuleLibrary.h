#pragma once

#include <ModuleDescriptor.h>
#include <PanguModuleExport.h>
#include <Result.h>

#include <filesystem>

namespace pg::module::detail
{
struct ModuleManagerTestAccess;

// 管理操作由调用方串行协调。Query/其返回的借用数据只能在持有期间使用。
class PG_MODULE_API NativeModuleLibrary final
{
public:
    ~NativeModuleLibrary();
    NativeModuleLibrary(const NativeModuleLibrary&) = delete;
    NativeModuleLibrary& operator=(const NativeModuleLibrary&) = delete;
    NativeModuleLibrary(NativeModuleLibrary&& other) noexcept;
    NativeModuleLibrary& operator=(NativeModuleLibrary&& other) noexcept;

    [[nodiscard]] static Result<NativeModuleLibrary> Open(const std::filesystem::path& path) noexcept;
    [[nodiscard]] Result<QueryModuleFn> ResolveQuery() const noexcept;
    // 成功才移除本对象的持有，不承诺最后一个 OS 持有已经消失。
    [[nodiscard]] Status Release() noexcept;

private:
    friend struct ModuleManagerTestAccess;
    explicit NativeModuleLibrary(void* handle) noexcept;
    [[nodiscard]] Status CloseNative() noexcept;
    void* Handle{};
    bool FailNextClose{};
};
} // namespace pg::module::detail
