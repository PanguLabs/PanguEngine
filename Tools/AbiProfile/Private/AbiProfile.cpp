#include "AbiProfile.h"

#include "JsonSchema.h"
#include "Sha256.h"
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <string>

namespace pg::abi
{
std::string Fingerprint(const nlohmann::json& profile)
{
    constexpr auto kPrefix = std::to_array("PANGU_ABI_PROFILE_V1");
    std::string bytes(kPrefix.data(), kPrefix.size()); // Includes one NUL byte.
    bytes += CanonicalJson(profile);
    return Sha256Hex(bytes);
}
} // namespace pg::abi
