#include <Error.h>
#include <Result.h>

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

bool TestErrorPropagation()
{
    const auto succeeded = PropagateDummy(false);
    const auto failed = PropagateDummy(true);
    if (!succeeded || *succeeded != 124 || failed || failed.error() != kWithNative)
    {
        return false;
    }

    if (pg::MakeError(kDomain, 42) != kSimple)
    {
        return false;
    }

    const auto statusOk = CheckDummy(false);
    const auto statusFailed = CheckDummy(true);
    return statusOk && !statusFailed && statusFailed.error() == kSimple;
}
} // namespace

int main() { return TestErrorPropagation() ? 0 : 1; }
