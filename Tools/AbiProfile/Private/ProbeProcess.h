#pragma once

#include "Process.h"

#include <string_view>

namespace pg::abi
{
// Captured probe failures emit bounded evidence to stderr. The caller retains
// its domain error and stdout FailureReport interface; successful probes stay quiet.
[[nodiscard]] Result<ProcessResult> RunProbe(const ProcessRequest& request, std::string_view stage,
                                             FailureReport& report);
} // namespace pg::abi
