#pragma once

#include <Error.h>

#include <cstdint>

namespace pg::module
{
// Allocated independently of ModuleId. Values are explicit; API/ABI remains unfrozen.
inline constexpr auto kModuleErrorDomain = pg::ErrorDomainId::Constant<0x70672e6d6f64756cULL>();

enum class ModuleError : std::uint8_t
{
    kInvalidDefinition = 1,
    kInvalidDescriptor = 2,
    kLimitExceeded = 3,
    kDuplicateModule = 4,
    kInvalidDependency = 5,
    kMissingDependency = 6,
    kVersionMismatch = 7,
    kInvalidOptionalBinding = 8,
    kDependencyCycle = 9,
    kNotFound = 10,
    kInvalidState = 11,
    kDependencyNotRegistered = 12,
    kDeclarationConflict = 13,
    kInvalidDeclaration = 14,
    kBusy = 15,
    kNotReady = 16,
    kTypeMismatch = 17,
    kInvalidScope = 18,
    kScopeNotFound = 19,
    kDependencyInUse = 20,
    kDependencyNotActive = 21,
    kUndeclaredDependency = 22,
};

[[nodiscard]] constexpr pg::Error MakeModuleError(ModuleError code) noexcept
{
    return {kModuleErrorDomain, static_cast<std::uint32_t>(code)};
}
} // namespace pg::module
