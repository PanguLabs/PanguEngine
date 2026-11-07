#pragma once

#include "FixtureModules.h"
#include <PanguPhysicsExport.h>

namespace pg::fixture
{
[[nodiscard]] PG_PHYSICS_API const module::ModuleDefinition* QueryPhysics() noexcept;
} // namespace pg::fixture
