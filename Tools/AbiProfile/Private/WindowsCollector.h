#pragma once

#include "CollectionState.h"

#include <string_view>

namespace pg::abi
{
// Validates the actual ApplicationPlatform element, not an unrelated XML tag.
[[nodiscard]] bool WindowsSdkManifestMatches(std::string_view xml, std::string_view expectedVersion);
[[nodiscard]] Result<CompileObservation> InspectWindowsCompileInputs(const CollectionRequest& request,
                                                                     std::span<const CompileUnit> units,
                                                                     FailureReport& report);
[[nodiscard]] Result<CollectedProfile> CollectWindowsPlatformProfile(const CollectionRequest& request,
                                                                     std::span<const CompileUnit> units,
                                                                     const CompileObservation& observation,
                                                                     FailureReport& report);
} // namespace pg::abi
