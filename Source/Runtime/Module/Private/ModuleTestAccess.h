#pragma once

#include "NativeModuleLibrary.h"
#include <ModuleManager.h>

namespace pg::module::detail
{
// 私有、逐实例的一次性故障；后续重试必须执行真实原生释放。
struct ModuleManagerTestAccess
{
    static void FailNextClose(NativeModuleLibrary& library) noexcept { library.FailNextClose = true; }
    static PG_MODULE_API void FailNextNativeClose(ModuleManager& manager, ModuleHandle handle) noexcept;
    static PG_MODULE_API void FailNextRollbackClose(ModuleManager& manager) noexcept;
};
} // namespace pg::module::detail
