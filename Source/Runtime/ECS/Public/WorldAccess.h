#pragma once

#include <PanguECSExport.h>
#include <Result.h>
#include <ScopeAccess.h>
#include <ScopeIdentity.h>

namespace pg::engine
{
class EngineHost;
} // namespace pg::engine

namespace pg::ecs
{
class World;

[[nodiscard]] PG_ECS_API Result<module::ScopeAccess<World>> AcquireWorld(const engine::EngineHost& host,
                                                                         module::ScopeHandle scope) noexcept;
} // namespace pg::ecs
