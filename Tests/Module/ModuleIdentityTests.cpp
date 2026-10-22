#include "ModuleIdentityParse.h"

#include <ModuleIdentity.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>

namespace
{
using pg::module::ModuleId;
using pg::module::ModuleVersion;
using pg::module::detail::ModuleIdParseError;

static_assert(std::is_trivially_copyable_v<ModuleId>);
static_assert(std::is_standard_layout_v<ModuleId>);
static_assert(sizeof(ModuleId) == 16);

constexpr ModuleVersion kRelease{.Major = 1, .Minor = 2, .Patch = 3};
static_assert(kRelease == ModuleVersion{.Major = 1, .Minor = 2, .Patch = 3});
static_assert(kRelease < ModuleVersion{.Major = 1, .Minor = 2, .Patch = 4});
static_assert(kRelease < ModuleVersion{.Major = 1, .Minor = 3, .Patch = 0});
static_assert(ModuleVersion{.Major = 0, .Minor = 0, .Patch = 0} < kRelease);
static_assert(!ModuleId{}.IsValid());
constexpr ModuleId kCompileTimeId{.Bytes = {std::byte{0x12}, std::byte{0xab}}};
static_assert(kCompileTimeId.IsValid());
constexpr auto kCompileTimeHex = pg::module::FormatModuleId(kCompileTimeId);
static_assert(std::string_view{kCompileTimeHex.data(), kCompileTimeHex.size()}.starts_with("12ab"));
static_assert(kCompileTimeHex.back() == '0');

bool TestParseAndFormat()
{
    constexpr std::string_view kHex = "00112233445566778899aabbccddeeff";
    const auto parsed = pg::module::detail::ParseModuleId(kHex);
    if (!parsed || !parsed->IsValid())
    {
        return false;
    }

    const auto formatted = pg::module::FormatModuleId(*parsed);
    if (std::string_view{formatted.data(), formatted.size()} != kHex)
    {
        return false;
    }

    const auto secondParse = pg::module::detail::ParseModuleId(kHex);
    return parsed->Bytes.front() == std::byte{0x00} && parsed->Bytes.back() == std::byte{0xff} && secondParse &&
           *secondParse == *parsed;
}

bool TestInvalidIdentities()
{
    constexpr std::string_view kShort = "001122";
    constexpr std::string_view kUppercase = "00112233445566778899AABBCCDDEEFF";
    constexpr std::string_view kNonHex = "00112233445566778899aabbccddeefg";
    constexpr std::string_view kZero = "00000000000000000000000000000000";

    const auto shortId = pg::module::detail::ParseModuleId(kShort);
    const auto upperId = pg::module::detail::ParseModuleId(kUppercase);
    const auto nonHexId = pg::module::detail::ParseModuleId(kNonHex);
    const auto zeroId = pg::module::detail::ParseModuleId(kZero);

    return !shortId && shortId.error() == ModuleIdParseError::kInvalidLength && !upperId &&
           upperId.error() == ModuleIdParseError::kInvalidCharacter && !nonHexId &&
           nonHexId.error() == ModuleIdParseError::kInvalidCharacter && !zeroId &&
           zeroId.error() == ModuleIdParseError::kZeroId;
}

bool TestRoundTrip()
{
    constexpr std::string_view kHex = "0123456789abcdef0123456789abcdef";
    const auto parsed = pg::module::detail::ParseModuleId(kHex);
    if (!parsed)
    {
        return false;
    }

    const auto roundTrip = pg::module::FormatModuleId(*parsed);
    return std::string_view{roundTrip.data(), roundTrip.size()} == kHex;
}
} // namespace

int main() { return TestParseAndFormat() && TestInvalidIdentities() && TestRoundTrip() ? 0 : 1; }
