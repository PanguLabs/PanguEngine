#pragma once

#include <ModuleDescriptor.h>
#include <MyGameGameplayExport.h>

namespace pg::fixture
{
[[nodiscard]] PG_GAMEPLAY_API const module::ModuleDefinition* QueryGameplay() noexcept;
} // namespace pg::fixture
