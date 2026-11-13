#pragma once

#include <Error.h>
#include <PanguFoundationExport.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <source_location>
#include <string_view>

namespace pg
{
namespace detail
{
struct FailureTextSlice
{
    std::uint16_t Offset{};
    std::uint16_t Length{};
};
} // namespace detail

// 独立拥有型现场；返回的文本视图仅借用当前 Site，不借用原始 Module。
class PG_FOUNDATION_API FailureSite final
{
public:
    FailureSite(Error cause, std::string_view message,
                std::source_location location = std::source_location::current()) noexcept;

    // 作为现场的公开值身份，供不依赖原始错误字典的读取方使用。
    Error Cause; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)

    [[nodiscard]] std::string_view Message() const noexcept;
    [[nodiscard]] std::string_view File() const noexcept;
    [[nodiscard]] std::string_view Function() const noexcept;
    [[nodiscard]] bool OriginKnown() const noexcept { return SourceLine != 0; }
    [[nodiscard]] std::uint32_t Line() const noexcept { return SourceLine; }
    [[nodiscard]] std::uint32_t Column() const noexcept { return SourceColumn; }
    [[nodiscard]] bool Truncated() const noexcept { return HasTruncation; }

private:
    friend class FailureReport;
    std::array<char, 400> Text{};
    detail::FailureTextSlice MessageText{};
    detail::FailureTextSlice FileText{};
    detail::FailureTextSlice FunctionText{};
    std::uint32_t SourceLine{};
    std::uint32_t SourceColumn{};
    bool HasTruncation{};
};

static_assert(sizeof(FailureSite) <= 512);

enum class FailureEntryKind : std::uint8_t
{
    kContext,
    kSecondary,
};

// 固定预算快照；内部只存数值、内联字节和偏移，默认复制/移动不形成自引用。
class PG_FOUNDATION_API FailureReport final
{
public:
    [[nodiscard]] std::optional<FailureSite> PrimarySite() const noexcept;
    [[nodiscard]] bool Truncated() const noexcept { return HasTruncation; }
    void Clear() noexcept;

    [[nodiscard]] std::size_t EntryCount() const noexcept { return Count; }
    [[nodiscard]] std::optional<FailureEntryKind> EntryKind(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<FailureSite> SecondarySite(std::size_t index) const noexcept;
    // Context 视图借用当前 Report，在修改、Clear 或销毁后失效。
    [[nodiscard]] std::string_view ContextKey(std::size_t index) const noexcept;
    [[nodiscard]] std::string_view ContextValue(std::size_t index) const noexcept;

private:
    friend class FailureContext;
    struct PrimaryStorage
    {
        std::optional<FailureSite> Site;
        // 主现场独占 512 B；次项永远不能使用这部分空间。
        std::array<std::byte, 512 - sizeof(std::optional<FailureSite>)> Reserved{};
    };
    static_assert(sizeof(PrimaryStorage) == 512);

    struct Entry
    {
        std::uint64_t Domain{};
        std::uint64_t NativeSource{};
        std::int64_t NativeValue{};
        std::uint32_t Code{};
        std::uint32_t SourceLine{};
        std::uint32_t SourceColumn{};
        detail::FailureTextSlice MessageText{};
        detail::FailureTextSlice FileText{};
        detail::FailureTextSlice FunctionText{};
        FailureEntryKind Kind{};
        bool HasNative{};
        bool HasTruncation{};
    };

    void SetPrimary(Error cause, std::string_view message, std::source_location location) noexcept;
    void AddContext(std::string_view key, std::string_view value) noexcept;
    void AddSecondary(Error cause, std::string_view message, std::source_location location) noexcept;
    void AddSecondary(const FailureSite& site) noexcept;

    PrimaryStorage Primary{};
    std::array<Entry, 8> Entries{};
    std::array<char, 1024> Text{};
    std::uint16_t TextUsed{};
    std::uint8_t Count{};
    bool HasTruncation{};
};

static_assert(sizeof(FailureReport) <= 2048);
} // namespace pg
