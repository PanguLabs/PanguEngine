#include <Error.h>
#include <FailureReport.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <source_location>
#include <span>
#include <string_view>

namespace
{
[[nodiscard]] pg::detail::FailureTextSlice CopyText(std::span<char> destination, std::uint16_t& used,
                                                    std::string_view text, bool& truncated) noexcept
{
    const auto available = destination.subspan(used);
    const auto length = std::min(text.size(), available.size());
    std::ranges::copy(text.substr(0, length), available.begin());
    const pg::detail::FailureTextSlice slice{.Offset = used, .Length = static_cast<std::uint16_t>(length)};
    used = static_cast<std::uint16_t>(used + length);
    truncated = truncated || length != text.size();
    return slice;
}

[[nodiscard]] std::string_view ReadText(std::span<const char> storage, pg::detail::FailureTextSlice slice) noexcept
{
    const auto text = storage.subspan(slice.Offset, slice.Length);
    return {text.data(), text.size()};
}
} // namespace

namespace pg
{
FailureSite::FailureSite(Error cause, std::string_view message, std::source_location location) noexcept
    : Cause(cause)
    , SourceLine(location.line())
    , SourceColumn(location.column())
{
    std::uint16_t used{};
    MessageText = CopyText(Text, used, message, HasTruncation);
    if (OriginKnown())
    {
        FileText = CopyText(Text, used, location.file_name(), HasTruncation);
        FunctionText = CopyText(Text, used, location.function_name(), HasTruncation);
    }
}

std::string_view FailureSite::Message() const noexcept { return ReadText(Text, MessageText); }
std::string_view FailureSite::File() const noexcept { return ReadText(Text, FileText); }
std::string_view FailureSite::Function() const noexcept { return ReadText(Text, FunctionText); }

std::optional<FailureSite> FailureReport::PrimarySite() const noexcept { return Primary.Site; }

void FailureReport::Clear() noexcept { *this = FailureReport{}; }

std::optional<FailureEntryKind> FailureReport::EntryKind(std::size_t index) const noexcept
{
    if (index >= Count)
    {
        return std::nullopt;
    }
    return std::span{Entries}.subspan(index).front().Kind;
}

std::optional<FailureSite> FailureReport::SecondarySite(std::size_t index) const noexcept
{
    if (EntryKind(index) != FailureEntryKind::kSecondary)
    {
        return std::nullopt;
    }
    const auto& entry = std::span{Entries}.subspan(index).front();
    // Domain 由有效 Error 写入；Context 条目已在上面排除。
    const auto domain = ErrorDomainId::TryFromRaw(entry.Domain);
    if (!domain)
    {
        return std::nullopt;
    }
    const auto cause = entry.HasNative
                           ? Error{*domain,
                                   entry.Code,
                                   {.Source = NativeErrorSourceId{entry.NativeSource}, .Value = entry.NativeValue}}
                           : Error{*domain, entry.Code};
    FailureSite site{cause, ReadText(Text, entry.MessageText), {}};
    auto used = site.MessageText.Length;
    site.FileText = CopyText(site.Text, used, ReadText(Text, entry.FileText), site.HasTruncation);
    site.FunctionText = CopyText(site.Text, used, ReadText(Text, entry.FunctionText), site.HasTruncation);
    site.SourceLine = entry.SourceLine;
    site.SourceColumn = entry.SourceColumn;
    site.HasTruncation = site.HasTruncation || entry.HasTruncation;
    return site;
}

std::string_view FailureReport::ContextKey(std::size_t index) const noexcept
{
    if (EntryKind(index) != FailureEntryKind::kContext)
    {
        return {};
    }
    return ReadText(Text, std::span{Entries}.subspan(index).front().FileText);
}

std::string_view FailureReport::ContextValue(std::size_t index) const noexcept
{
    if (EntryKind(index) != FailureEntryKind::kContext)
    {
        return {};
    }
    return ReadText(Text, std::span{Entries}.subspan(index).front().MessageText);
}

void FailureReport::SetPrimary(Error cause, std::string_view message, std::source_location location) noexcept
{
    if (Primary.Site)
    {
        if (Primary.Site->Cause != cause)
        {
            AddSecondary(cause, message, location);
        }
        return;
    }
    Primary.Site.emplace(cause, message, location);
    HasTruncation = HasTruncation || Primary.Site->Truncated();
}

void FailureReport::AddContext(std::string_view key, std::string_view value) noexcept
{
    if (Count == Entries.size())
    {
        HasTruncation = true;
        return;
    }
    auto& entry = std::span{Entries}.subspan(Count++).front();
    entry.Kind = FailureEntryKind::kContext;
    entry.FileText = CopyText(Text, TextUsed, key, entry.HasTruncation);
    entry.MessageText = CopyText(Text, TextUsed, value, entry.HasTruncation);
    HasTruncation = HasTruncation || entry.HasTruncation;
}

void FailureReport::AddSecondary(Error cause, std::string_view message, std::source_location location) noexcept
{
    AddSecondary(FailureSite{cause, message, location});
}

void FailureReport::AddSecondary(const FailureSite& site) noexcept
{
    if (Count == Entries.size())
    {
        HasTruncation = true;
        return;
    }
    const auto cause = site.Cause;
    auto& entry = std::span{Entries}.subspan(Count++).front();
    entry.Kind = FailureEntryKind::kSecondary;
    entry.Domain = cause.Domain().Value();
    entry.Code = cause.Code();
    if (const auto native = cause.Native())
    {
        entry.HasNative = true;
        entry.NativeSource = native->Source.Value;
        entry.NativeValue = native->Value;
    }
    entry.SourceLine = site.Line();
    entry.SourceColumn = site.Column();
    entry.HasTruncation = site.Truncated();
    entry.MessageText = CopyText(Text, TextUsed, site.Message(), entry.HasTruncation);
    entry.FileText = CopyText(Text, TextUsed, site.File(), entry.HasTruncation);
    entry.FunctionText = CopyText(Text, TextUsed, site.Function(), entry.HasTruncation);
    HasTruncation = HasTruncation || entry.HasTruncation;
}
} // namespace pg
