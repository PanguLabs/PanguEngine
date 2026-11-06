#include <Error.h>
#include <Result.h>

#include <gtest/gtest.h>

#include <concepts>
#include <cstdint>
#include <expected>
#include <type_traits>

namespace
{
using pg::Error;
using pg::ErrorDomainId;
using pg::NativeErrorCode;
using pg::NativeErrorSourceId;

constexpr ErrorDomainId kDomain = ErrorDomainId::Constant<0x12345678ULL>();
constexpr ErrorDomainId kDifferentDomain = ErrorDomainId::Constant<0x12345679ULL>();
constexpr NativeErrorCode kNative{.Source = NativeErrorSourceId{7}, .Value = -17};
static_assert(!std::is_constructible_v<ErrorDomainId, std::uint64_t>);
static_assert(!ErrorDomainId::TryFromRaw(0));
static_assert(ErrorDomainId::TryFromRaw(0x12345678ULL) == kDomain);
constexpr Error kSimple{kDomain, 42};
constexpr Error kWithNative{kDomain, 42, kNative};

static_assert(!std::is_default_constructible_v<Error>);
static_assert(std::is_trivially_copyable_v<Error>);
static_assert(std::is_standard_layout_v<Error>);
static_assert(std::is_nothrow_copy_constructible_v<Error>);
static_assert(std::is_nothrow_destructible_v<Error>);
static_assert(sizeof(Error) <= 64);
static_assert(kDomain.IsValid());
static_assert(kSimple.Domain() == kDomain);
static_assert(kSimple.Code() == 42);
static_assert(!kSimple.Native().has_value());
static_assert(kWithNative.Native().has_value());
static_assert(kWithNative.Native()->Source == NativeErrorSourceId{7});
static_assert(kWithNative.Native()->Value == -17);
static_assert(kSimple != kWithNative);
static_assert(kSimple != Error{kDifferentDomain, 42});

static_assert(std::same_as<pg::Result<int>, std::expected<int, Error>>);
static_assert(std::same_as<pg::Status, std::expected<void, Error>>);
static_assert(std::same_as<pg::Result<void>, pg::Status>);

[[nodiscard]] pg::Result<int> ParseDummy(bool fail) noexcept
{
    if (fail)
    {
        return std::unexpected(kWithNative);
    }
    return 123;
}

[[nodiscard]] pg::Result<int> PropagateDummy(bool fail) noexcept
{
    auto value = ParseDummy(fail);
    if (!value)
    {
        return std::unexpected(value.error());
    }
    return *value + 1;
}

[[nodiscard]] pg::Status CheckDummy(bool fail) noexcept
{
    if (fail)
    {
        return std::unexpected(kSimple);
    }
    return {};
}

[[nodiscard]] constexpr bool TestExpectedValues() noexcept
{
    constexpr pg::Result<int> kResult{17};
    constexpr pg::Status kSuccess{};
    constexpr pg::Status kFailure{std::unexpected(kSimple)};
    return kResult && *kResult == 17 && kSuccess && !kFailure && kFailure.error() == kSimple;
}

static_assert(TestExpectedValues());

TEST(Error, PreservesDomainCodeAndNativeValue)
{
    EXPECT_EQ(pg::MakeError(kDomain, 42), kSimple);
    EXPECT_EQ(kSimple.Domain(), kDomain);
    EXPECT_EQ(kSimple.Code(), 42);
    EXPECT_FALSE(kSimple.Native().has_value());
    const auto native = kWithNative.Native();
    if (native.has_value())
    {
        EXPECT_EQ(native->Source, NativeErrorSourceId{7});
        EXPECT_EQ(native->Value, -17);
    }
    else
    {
        FAIL() << "Native diagnostics must be present";
    }
    EXPECT_NE(kSimple, kWithNative);
    EXPECT_NE(kSimple, (Error{kDifferentDomain, 42}));
}

TEST(Result, SuccessPreservesExactValue)
{
    const pg::Result<int> result{17};
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 17);
}

TEST(Result, SuccessfulPropagationReturns124)
{
    const auto succeeded = PropagateDummy(false);
    ASSERT_TRUE(succeeded.has_value());
    EXPECT_EQ(*succeeded, 124);
}

TEST(Result, FailedPropagationPreservesOriginalError)
{
    const auto failed = PropagateDummy(true);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error(), kWithNative);
    const auto native = failed.error().Native();
    if (native.has_value())
    {
        EXPECT_EQ(native->Source, kNative.Source);
        EXPECT_EQ(native->Value, kNative.Value);
    }
    else
    {
        FAIL() << "Propagation must preserve native diagnostics";
    }
}

TEST(Status, SuccessHasNoError)
{
    const auto statusOk = CheckDummy(false);
    EXPECT_TRUE(statusOk.has_value());
}

TEST(Status, FailurePreservesOriginalError)
{
    const auto statusFailed = CheckDummy(true);
    ASSERT_FALSE(statusFailed.has_value());
    EXPECT_EQ(statusFailed.error(), kSimple);
}
} // namespace
