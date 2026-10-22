#pragma once

#include <compare>
#include <cstdint>
#include <optional>

namespace pg
{
// Candidate representation (ABI layout remains unfrozen).
// A domain identifier is not a ModuleId and zero cannot be constructed.
class ErrorDomainId final
{
public:
    template <std::uint64_t Id>
    [[nodiscard]] static consteval ErrorDomainId Constant() noexcept
    {
        static_assert(Id != 0, "Error domain ID zero is reserved.");
        return ErrorDomainId{Id};
    }

    [[nodiscard]] static constexpr std::optional<ErrorDomainId> TryFromRaw(std::uint64_t value) noexcept
    {
        if (value == 0)
        {
            return std::nullopt;
        }
        return ErrorDomainId{value};
    }

    [[nodiscard]] constexpr std::uint64_t Value() const noexcept { return RawValue; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return RawValue != 0; }

    constexpr auto operator<=>(const ErrorDomainId&) const noexcept = default;

private:
    explicit constexpr ErrorDomainId(std::uint64_t value) noexcept
        : RawValue(value)
    {
    }

    std::uint64_t RawValue;
};

// Native source identifies the original operating-system or external error namespace.
struct NativeErrorSourceId
{
    std::uint64_t Value;

    explicit constexpr NativeErrorSourceId(std::uint64_t value) noexcept
        : Value(value)
    {
    }

    constexpr auto operator<=>(const NativeErrorSourceId&) const noexcept = default;
};

struct NativeErrorCode
{
    NativeErrorSourceId Source;
    std::int64_t Value;

    constexpr auto operator<=>(const NativeErrorCode&) const noexcept = default;
};

// Value-only error with no heap, borrowed pointers, virtual callbacks, or Module code references.
class Error final
{
public:
    Error() = delete;

    constexpr Error(ErrorDomainId domain, std::uint32_t code) noexcept
        : DomainValue(domain)
        , CodeValue(code)
    {
    }

    constexpr Error(ErrorDomainId domain, std::uint32_t code, NativeErrorCode native) noexcept
        : DomainValue(domain)
        , CodeValue(code)
        , NativeValue(native)
    {
    }

    [[nodiscard]] constexpr ErrorDomainId Domain() const noexcept { return DomainValue; }
    [[nodiscard]] constexpr std::uint32_t Code() const noexcept { return CodeValue; }
    [[nodiscard]] constexpr std::optional<NativeErrorCode> Native() const noexcept { return NativeValue; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return DomainValue.IsValid(); }

    constexpr auto operator<=>(const Error&) const noexcept = default;

private:
    ErrorDomainId DomainValue;
    std::uint32_t CodeValue;
    std::optional<NativeErrorCode> NativeValue;
};

// Implemented in the owning Foundation Module; not required for constexpr usage.
[[nodiscard]] Error MakeError(ErrorDomainId domain, std::uint32_t code) noexcept;
} // namespace pg
