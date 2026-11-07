#pragma once

#include <ScopeAccess.h>
#include <atomic>
#include <cstdint>
#include <memory>

namespace pg::module::detail
{
struct ModuleCodeControl
{
    // 与 ScopeBorrow 共用接纳线性化点，但不把外部 Pin 计入资源借用。
    std::shared_ptr<ScopeGate> Gate = MakeScopeGate();
    std::atomic<std::uint64_t> ExternalPins;
    [[nodiscard]] bool HasExternalUsers() const noexcept
    {
        return Gate->Count() != 0 || ExternalPins.load(std::memory_order_acquire) != 0;
    }
};
[[nodiscard]] std::shared_ptr<ModuleCodeControl> MakeModuleCodeControl() noexcept;
} // namespace pg::module::detail
