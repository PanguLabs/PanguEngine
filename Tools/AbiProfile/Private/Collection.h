#pragma once

#include "CollectionRequest.h"
#include "CollectionState.h"

#include <span>
#include <string>

namespace pg::abi
{
[[nodiscard]] Result<CompileObservation>
InspectCompileInputs(const CollectionRequest& request, std::span<const CompileUnit> units, FailureReport& report);
[[nodiscard]] Result<CollectedProfile> CollectPlatformProfile(const CollectionRequest& request,
                                                              std::span<const CompileUnit> units,
                                                              const CompileObservation& observation,
                                                              FailureReport& report);
[[nodiscard]] Status Collect(const CollectionRequest& request, FailureReport& report);
[[nodiscard]] int RunCompilerInvocation(const CollectionRequest& request, std::span<const std::string> arguments,
                                        FailureReport& report);
} // namespace pg::abi
