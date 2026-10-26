#pragma once

#include "CollectionRequest.h"

namespace pg::abi
{
// Prepare signature headers of every reachable technical STATIC SCC before
// CMake begins compiling any of its members.
[[nodiscard]] Status GuardTechnicalClosure(const CollectionRequest& root, FailureReport& report);

// Validate and publish technical STATIC SCCs in C++ dependency-first order,
// after CMake has finished every participating archive.
[[nodiscard]] Status CollectTechnicalClosure(const CollectionRequest& root, FailureReport& report);
} // namespace pg::abi
