#pragma once

#include "CollectionState.h"

namespace pg::abi
{
[[nodiscard]] Result<CompileObservation>
InspectAppleCompileInputs(const CollectionRequest& request, std::span<const CompileUnit> units, FailureReport& report);
[[nodiscard]] Result<CollectedProfile> CollectApplePlatformProfile(const CollectionRequest& request,
                                                                   std::span<const CompileUnit> units,
                                                                   const CompileObservation& observation,
                                                                   FailureReport& report);
} // namespace pg::abi
