#pragma once

#include <ModuleDescriptor.h>
#include <MyGameSharedProbeExport.h>

namespace pg::fixture
{
[[nodiscard]] PG_SHARED_PROBE_API const module::ModuleDefinition* QuerySharedProbe() noexcept;
} // namespace pg::fixture
