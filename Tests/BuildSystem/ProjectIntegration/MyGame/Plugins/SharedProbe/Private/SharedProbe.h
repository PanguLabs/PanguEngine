#pragma once

#include <ModuleDescriptor.h>
#include <PanguSharedProbeExport.h>

namespace pg::fixture
{
[[nodiscard]] PG_SHARED_PROBE_API int SharedProbeValue() noexcept;
[[nodiscard]] PG_SHARED_PROBE_API const module::ModuleDefinition* QuerySharedProbe() noexcept;
} // namespace pg::fixture
