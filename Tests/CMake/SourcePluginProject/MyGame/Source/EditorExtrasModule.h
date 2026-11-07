#pragma once

#include "FixtureModules.h"
#include <PanguEditorExtrasExport.h>

namespace pg::fixture
{
[[nodiscard]] PG_EDITOR_EXTRAS_API const module::ModuleDefinition* QueryEditorExtras() noexcept;
} // namespace pg::fixture
