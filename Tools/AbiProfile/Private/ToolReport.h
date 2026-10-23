#pragma once

#include <Error.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <source_location>
#include <string_view>
#include <system_error>
#include <utility>

namespace pg::abi
{
// Tool-private identities; these do not establish a Plugin ABI or Foundation report API.
inline constexpr auto kToolErrorDomain = ErrorDomainId::Constant<0x5047414249544f4fULL>();
inline constexpr NativeErrorSourceId kErrnoSource{0x50474552524e4f01ULL};
inline constexpr NativeErrorSourceId kSystemSource{0x5047535953540001ULL};
inline constexpr NativeErrorSourceId kStreamSource{0x5047494f53540001ULL};

enum class ToolError : std::uint8_t
{
    kArguments = 1,
    kPathConflict = 2,
    kResolvePath = 3,
    kReadInput = 4,
    kSchemaMismatch = 5,
    kInvalidSchema = 6,
    kInvalidProfile = 7,
    kRemoveOutput = 8,
    kOpenOutput = 9,
    kWriteOutput = 10,
    kFlushOutput = 11,
    kCloseFile = 12,
};

template <std::size_t Capacity>
struct ReportText
{
    std::array<char, Capacity> Bytes{};
    std::size_t Size{};

    [[nodiscard]] bool Assign(std::string_view text) noexcept
    {
        Size = std::min(text.size(), Capacity);
        std::copy_n(text.begin(), Size, Bytes.begin());
        return text.size() > Capacity;
    }

    [[nodiscard]] std::string_view View() const noexcept { return {Bytes.data(), Size}; }
};

struct FailureEntry
{
    Error Value{kToolErrorDomain, static_cast<std::uint32_t>(ToolError::kArguments)};
    ReportText<48> Stage;
    ReportText<512> Path;
    ReportText<256> SourceFile;
    ReportText<256> JsonPointer;
    ReportText<32> NativeCategory;
    std::uint_least32_t SourceLine{};
    std::optional<std::size_t> ByteOffset;
};

struct FailureReport
{
    static constexpr std::size_t kCapacity = 8;
    std::array<FailureEntry, kCapacity> Entries{};
    std::size_t Count{};
    bool Truncated{};
    bool CleanupIncomplete{};

    [[nodiscard]] Error Add(ToolError code, std::string_view stage, std::string_view path = {},
                            std::error_code native = {}, std::optional<std::size_t> byteOffset = std::nullopt,
                            std::string_view jsonPointer = {},
                            std::source_location origin = std::source_location::current()) noexcept
    {
        auto source = kStreamSource;
        if (native.category() == std::generic_category())
        {
            source = kErrnoSource;
        }
        else if (native.category() == std::system_category())
        {
            source = kSystemSource;
        }
        const auto numericCode = static_cast<std::uint32_t>(code);
        const Error error =
            native ? Error{kToolErrorDomain, numericCode, NativeErrorCode{.Source = source, .Value = native.value()}}
                   : Error{kToolErrorDomain, numericCode};
        if (Count == kCapacity)
        {
            Truncated = true;
            return error;
        }
        auto& entry = Entries.at(Count++);
        entry.Value = error;
        // Evaluate every copy even if an earlier field already overflowed.
        Truncated |= entry.Stage.Assign(stage);
        Truncated |= entry.Path.Assign(path);
        Truncated |= entry.SourceFile.Assign(origin.file_name());
        Truncated |= entry.JsonPointer.Assign(jsonPointer);
        Truncated |= entry.NativeCategory.Assign(native ? native.category().name() : "");
        entry.SourceLine = origin.line();
        entry.ByteOffset = byteOffset;
        return error;
    }

    // Collection owns fixed-capacity storage; JSON allocation happens only at delivery.
    void WriteJson(std::ostream& output) const
    {
        auto errors = nlohmann::json::array();
        for (std::size_t i = 0; i < Count; ++i)
        {
            const auto& entry = Entries.at(i);
            const auto& error = entry.Value;
            nlohmann::json item{
                {"domain", error.Domain().Value()},
                {"code", error.Code()},
                {"stage", entry.Stage.View()},
                {"path", entry.Path.View()},
                {"source_file", entry.SourceFile.View()},
                {"source_line", entry.SourceLine},
            };
            if (const auto native = error.Native())
            {
                item.emplace("native", nlohmann::json{
                                           {"source", native->Source.Value},
                                           {"code", native->Value},
                                           {"category", entry.NativeCategory.View()},
                                       });
            }
            if (entry.ByteOffset)
            {
                item.emplace("byte_offset", *entry.ByteOffset);
            }
            if (entry.JsonPointer.Size != 0)
            {
                item.emplace("json_pointer", entry.JsonPointer.View());
            }
            errors.push_back(std::move(item));
        }
        const nlohmann::json report{
            {"schema", 1},
            {"operation", "abi-profile"},
            {"errors", std::move(errors)},
            {"truncated", Truncated},
            {"cleanup_incomplete", CleanupIncomplete},
        };
        // A truncated path can end mid UTF-8 sequence; delivery must remain valid JSON.
        output << report.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
    }
};
} // namespace pg::abi
