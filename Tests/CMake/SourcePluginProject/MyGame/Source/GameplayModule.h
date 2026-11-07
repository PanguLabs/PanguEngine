#pragma once

#include "FixtureModules.h"
#include <PanguGameplayExport.h>

namespace pg::fixture
{
[[nodiscard]] PG_GAMEPLAY_API const module::ModuleDefinition* QueryGameplay() noexcept;
} // namespace pg::fixture
