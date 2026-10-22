#pragma once

#include <ModuleIdentity.h>

#include <cstdint>
#include <expected>
#include <string_view>

namespace pg::module::detail
{
enum class ModuleIdParseError : std::uint8_t
{
    kInvalidLength,
    kInvalidCharacter,
    kZeroId,
};

// Internal parser. The future Public API must translate domain errors to pg::Error.
[[nodiscard]] std::expected<ModuleId, ModuleIdParseError> ParseModuleId(std::string_view text) noexcept;
} // namespace pg::module::detail
