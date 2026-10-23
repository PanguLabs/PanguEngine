// This TU must compile without nlohmann/json.hpp entering the include graph.
#include "AbiProfile.h"
#include <nlohmann/json_fwd.hpp>

#ifdef INCLUDE_NLOHMANN_JSON_HPP_
#error "AbiProfile.h must not transitively include the full nlohmann/json.hpp"
#endif

#include <string>
#include <type_traits>
#include <utility>

static_assert(std::is_same_v<decltype(pg::abi::Fingerprint(std::declval<const nlohmann::json&>())), std::string>);
