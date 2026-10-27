#pragma once

#include "CollectionState.h"

namespace pg::abi
{
[[nodiscard]] Result<CompileObservation>
InspectLinuxCompileInputs(const CollectionRequest& request, std::span<const CompileUnit> units, FailureReport& report);
[[nodiscard]] Result<CollectedProfile> CollectLinuxPlatformProfile(const CollectionRequest& request,
                                                                   std::span<const CompileUnit> units,
                                                                   const CompileObservation& observation,
                                                                   FailureReport& report);
} // namespace pg::abi
