#include "ModuleIdentityParse.h"

#include <ModuleIdentity.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>

namespace pg::module
{
namespace
{
constexpr std::uint8_t kInvalidDigit = 0xff;

[[nodiscard]] constexpr std::uint8_t ParseHexDigit(char digit) noexcept
{
    if (digit >= '0' && digit <= '9')
    {
        return static_cast<std::uint8_t>(digit - '0');
    }
    if (digit >= 'a' && digit <= 'f')
    {
        return static_cast<std::uint8_t>(digit - 'a' + 10);
    }
    return kInvalidDigit;
}

} // namespace

namespace detail
{
std::expected<ModuleId, ModuleIdParseError> ParseModuleId(std::string_view text) noexcept
{
    ModuleId id;
    if (text.size() != id.Bytes.size() * 2)
    {
        return std::unexpected(ModuleIdParseError::kInvalidLength);
    }

    auto remaining = text;
    for (auto& value : id.Bytes)
    {
        const std::uint8_t high = ParseHexDigit(remaining.front());
        remaining.remove_prefix(1);
        const std::uint8_t low = ParseHexDigit(remaining.front());
        remaining.remove_prefix(1);
        if (high == kInvalidDigit || low == kInvalidDigit)
        {
            return std::unexpected(ModuleIdParseError::kInvalidCharacter);
        }

        const auto combined =
            static_cast<std::uint8_t>((static_cast<unsigned int>(high) * 16U) + static_cast<unsigned int>(low));
        value = static_cast<std::byte>(combined);
    }

    if (!id.IsValid())
    {
        return std::unexpected(ModuleIdParseError::kZeroId);
    }
    return id;
}
} // namespace detail

} // namespace pg::module
