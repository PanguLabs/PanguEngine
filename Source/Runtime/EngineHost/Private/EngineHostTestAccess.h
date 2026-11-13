#pragma once

#include <EngineHost.h>
#include <LogSystem.h>

namespace pg::engine::detail
{
// 仅用于逐实例的原生释放故障及真实外部 Pin 验证。
struct EngineHostTestAccess
{
    [[nodiscard]] static PG_ENGINE_HOST_API module::ModuleManager* Manager(EngineHost& host) noexcept;
    [[nodiscard]] static PG_ENGINE_HOST_API log::LogSystem& Log(EngineHost& host) noexcept;
};
} // namespace pg::engine::detail
