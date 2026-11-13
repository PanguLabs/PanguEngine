#pragma once

#include <Error.h>
#include <ModuleIdentity.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace pg::log
{
enum class Level : std::uint8_t
{
    kTrace,
    kDebug,
    kInfo,
    kWarning,
    kError,
    kOff,
};

enum class OutputMask : std::uint8_t
{
    kNone = 0,
    kConsole = 1,
    kFile = 2,
};
[[nodiscard]] constexpr OutputMask operator|(OutputMask left, OutputMask right) noexcept
{
    // 固定位宽 enum 表示位集合，组合值不必是单个枚举项。
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    return static_cast<OutputMask>(static_cast<unsigned int>(left) | static_cast<unsigned int>(right));
}
[[nodiscard]] constexpr OutputMask operator&(OutputMask left, OutputMask right) noexcept
{
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) 同上，允许位集合交集。
    return static_cast<OutputMask>(static_cast<unsigned int>(left) & static_cast<unsigned int>(right));
}
[[nodiscard]] constexpr bool HasOutput(OutputMask mask, OutputMask output) noexcept
{
    return (mask & output) == output;
}
enum class LogState : std::uint8_t
{
    kCold,
    kRunning,
    kClosing,
    kClosed,
};
enum class SubmitResult : std::uint8_t
{
    kAccepted,
    kFiltered,
    kDropped,
    kClosed,
    kDisabled,
};

// 只复制身份数值；Log 不反向依赖 Module 的 Scope/Declaration 实现。
struct LogScopeTag
{
    std::uint64_t Domain{};
    std::uint32_t Slot{};
    std::uint32_t Generation{};
};
struct LogTags
{
    std::optional<module::ModuleId> Module;
    std::optional<LogScopeTag> Scope;
    std::optional<std::array<std::byte, 16>> Declaration;
    std::optional<std::uint64_t> Operation;
};

inline constexpr auto kLogErrorDomain = ErrorDomainId::Constant<0x70676c6f67000001ULL>();
enum class LogError : std::uint8_t
{
    kInvalidConfig = 1,
    kOutOfMemory = 2,
    kNoOutputs = 3,
    kFileOpenFailed = 4,
    kIoFailed = 5,
    kPartitionBusy = 6,
    kBudgetExceeded = 7,
    kThreadStartFailed = 8,
    kBusy = 9,
    kInvalidState = 10,
    kSequenceExhausted = 11,
};
[[nodiscard]] constexpr Error MakeLogError(LogError code) noexcept
{
    return {kLogErrorDomain, static_cast<std::uint32_t>(code)};
}
} // namespace pg::log
