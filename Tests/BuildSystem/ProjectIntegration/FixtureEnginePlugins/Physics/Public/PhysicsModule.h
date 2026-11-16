#pragma once

#include <FixtureEnginePhysicsExport.h>
#include <ModuleDescriptor.h>

namespace pg::fixture
{
[[nodiscard]] PG_PHYSICS_API const module::ModuleDefinition* QueryPhysics() noexcept;
} // namespace pg::fixture
