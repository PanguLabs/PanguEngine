#include "ModuleIdentityParse.h"

#include <ModuleIdentity.h>

#include <cstddef>
#include <gtest/gtest.h>
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

TEST(ModuleIdentity, ParseAndFormat)
{
    constexpr std::string_view kHex = "00112233445566778899aabbccddeeff";
    const auto parsed = pg::module::detail::ParseModuleId(kHex);
    ASSERT_TRUE(parsed);
    EXPECT_TRUE(parsed->IsValid());
    const auto formatted = pg::module::FormatModuleId(*parsed);
    EXPECT_EQ((std::string_view{formatted.data(), formatted.size()}), kHex);
    EXPECT_EQ(parsed->Bytes.front(), std::byte{0x00});
    EXPECT_EQ(parsed->Bytes.back(), std::byte{0xff});
    const auto secondParse = pg::module::detail::ParseModuleId(kHex);
    ASSERT_TRUE(secondParse);
    EXPECT_EQ(*secondParse, *parsed);
}

TEST(ModuleIdentity, RejectsInvalidLength)
{
    const auto result = pg::module::detail::ParseModuleId("001122");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), ModuleIdParseError::kInvalidLength);
}

TEST(ModuleIdentity, RejectsUppercase)
{
    const auto result = pg::module::detail::ParseModuleId("00112233445566778899AABBCCDDEEFF");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), ModuleIdParseError::kInvalidCharacter);
}

TEST(ModuleIdentity, RejectsNonHexCharacter)
{
    const auto result = pg::module::detail::ParseModuleId("00112233445566778899aabbccddeefg");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), ModuleIdParseError::kInvalidCharacter);
}

TEST(ModuleIdentity, RejectsZero)
{
    const auto result = pg::module::detail::ParseModuleId("00000000000000000000000000000000");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), ModuleIdParseError::kZeroId);
}

TEST(ModuleIdentity, RoundTrip)
{
    constexpr std::string_view kHex = "0123456789abcdef0123456789abcdef";
    const auto parsed = pg::module::detail::ParseModuleId(kHex);
    ASSERT_TRUE(parsed);
    const auto roundTrip = pg::module::FormatModuleId(*parsed);
    EXPECT_EQ((std::string_view{roundTrip.data(), roundTrip.size()}), kHex);
}
} // namespace
