#pragma once

#include <PanguRendererExport.h>
#include <Result.h>
#include <ScopeAccess.h>

namespace pg::engine
{
class EngineHost;
}

namespace pg::renderer
{
class RendererSession;

[[nodiscard]] PG_RENDERER_API Result<module::ScopeAccess<RendererSession>>
AcquireRenderer(const engine::EngineHost& host) noexcept;
} // namespace pg::renderer
