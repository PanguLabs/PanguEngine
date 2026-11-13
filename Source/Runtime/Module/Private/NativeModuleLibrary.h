#pragma once

#include <FailureContext.h>
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

    [[nodiscard]] static Result<NativeModuleLibrary> Open(const std::filesystem::path& path,
                                                          FailureContext* failure = nullptr) noexcept;
    [[nodiscard]] Result<QueryModuleFn> ResolveQuery(FailureContext* failure = nullptr) const noexcept;
    // 成功才移除本对象的持有，不承诺最后一个 OS 持有已经消失。
    [[nodiscard]] Status Release(FailureContext* failure = nullptr) noexcept;

private:
    friend struct ModuleManagerTestAccess;
    NativeModuleLibrary(void* handle, std::filesystem::path path) noexcept;
    [[nodiscard]] Status CloseNative(FailureContext* failure) noexcept;
    static void AddPath(FailureContext* failure, const std::filesystem::path& path) noexcept;
    std::filesystem::path Path;
    void* Handle{};
    bool FailNextClose{};
};
} // namespace pg::module::detail
