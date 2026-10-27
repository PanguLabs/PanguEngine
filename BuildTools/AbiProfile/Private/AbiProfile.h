#pragma once

// Declaration-only header: clients of Fingerprint do not need the 1.6 MB
// json.hpp template implementation. Translation units doing JSON work include
// <nlohmann/json.hpp> themselves.
#include <nlohmann/json_fwd.hpp>

#include <string>

namespace pg::abi
{
// Build-time hash of a separately Schema-validated JSON Profile.
// SHA-256(ASCII("PANGU_ABI_PROFILE_V1") + NUL + canonical JSON).
[[nodiscard]] std::string Fingerprint(const nlohmann::json& profile);
} // namespace pg::abi
