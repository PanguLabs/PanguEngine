#include "HeaderTrace.h"
#include "ToolIo.h"
#include "ToolReport.h"
#include <Result.h>

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <expected>
#include <filesystem>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace pg::abi
{
namespace
{
[[nodiscard]] bool ControlledLibcxxMacro(std::string_view name)
{
    return name.starts_with("_LIBCPP_ABI_") || name.starts_with("_LIBCPP_HARDENING_") ||
           name.starts_with("_LIBCPP_DEBUG") || name.starts_with("_LIBCPP_ENABLE_") ||
           name.starts_with("_LIBCPP_DISABLE_") || name.starts_with("_LIBCPP_HAS_") || name.starts_with("_LIBCPP_NO_");
}

[[nodiscard]] bool NameCharacter(char character)
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '_';
}

[[nodiscard]] std::string_view TrimDirectiveSpace(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\f\v");
    return first == std::string_view::npos ? std::string_view{} : text.substr(first);
}

[[nodiscard]] std::size_t RawPrefixLength(std::string_view text)
{
    if (text.starts_with("u8R\""))
    {
        return 3;
    }
    if (text.starts_with("uR\"") || text.starts_with("UR\"") || text.starts_with("LR\""))
    {
        return 2;
    }
    return text.starts_with("R\"") ? 1 : 0;
}

// Only preprocessing directives are needed. First splice continued lines, then
// replace comments with whitespace and quoted tokens with one ordinary token.
// In particular, neither raw literal contents nor presumed #line paths are code.
[[nodiscard]] std::string DirectiveText(std::string_view source)
{
    if (source.starts_with("\xEF\xBB\xBF"))
    {
        source.remove_prefix(3);
    }
    std::string spliced;
    std::vector<std::size_t> physicalOffsets;
    for (std::size_t index = 0; index < source.size(); ++index)
    {
        if (source.at(index) == '\\')
        {
            auto end = index + 1;
            while (end < source.size() && (source.at(end) == ' ' || source.at(end) == '\t'))
            {
                ++end;
            }
            if (end < source.size() && source.at(end) == '\r')
            {
                ++end;
            }
            if (end < source.size() && source.at(end) == '\n')
            {
                index = end;
                continue;
            }
        }
        spliced += source.at(index);
        physicalOffsets.push_back(index);
    }
    const auto text = std::string_view(spliced);
    std::string result;
    for (std::size_t index = 0; index < text.size();)
    {
        const auto rest = text.substr(index);
        const auto rawPrefix = RawPrefixLength(rest);
        if (rest.starts_with("//"))
        {
            const auto end = text.find('\n', index + 2);
            index = end == std::string_view::npos ? text.size() : end;
            result += ' ';
        }
        else if (rest.starts_with("/*"))
        {
            const auto end = text.find("*/", index + 2);
            index = end == std::string_view::npos ? text.size() : end + 2;
            result += ' ';
        }
        else if (rawPrefix != 0)
        {
            const auto delimiterStart = index + rawPrefix + 1;
            const auto open = text.find('(', delimiterStart);
            if (open != std::string_view::npos && open - delimiterStart <= 16)
            {
                const auto delimiter = text.substr(delimiterStart, open - delimiterStart);
                // Phase-2 transformations are reverted inside raw literals.
                // A backslash/newline in their contents cannot manufacture a
                // closing delimiter that was absent from the physical file.
                const auto end = source.find(")" + std::string(delimiter) + "\"", physicalOffsets.at(open) + 1);
                index = end == std::string_view::npos
                            ? text.size()
                            : static_cast<std::size_t>(
                                  std::ranges::lower_bound(physicalOffsets, end + delimiter.size() + 2) -
                                  physicalOffsets.begin());
                result += 'x';
            }
            else
            {
                result += text.at(index++);
            }
        }
        else if (text.at(index) == '\"' || text.at(index) == '\'')
        {
            const auto quote = text.at(index++);
            while (index < text.size())
            {
                const auto character = text.at(index++);
                if (character == '\\' && index < text.size())
                {
                    ++index;
                }
                else if (character == quote)
                {
                    break;
                }
            }
            result += 'x';
        }
        else if (NameCharacter(text.at(index)) && (text.at(index) < '0' || text.at(index) > '9'))
        {
            // Keep identifiers whole: the 8 in u8 character/string prefixes
            // must never start a pp-number or consume the following quote.
            while (index < text.size() && NameCharacter(text.at(index)))
            {
                result += text.at(index++);
            }
        }
        else if ((text.at(index) >= '0' && text.at(index) <= '9') ||
                 (text.at(index) == '.' && index + 1 < text.size() && text.at(index + 1) >= '0' &&
                  text.at(index + 1) <= '9'))
        {
            // A digit separator is part of a pp-number, not a character literal.
            while (index < text.size() &&
                   (NameCharacter(text.at(index)) || text.at(index) == '.' || text.at(index) == '\'' ||
                    ((text.at(index) == '+' || text.at(index) == '-') && index > 0 &&
                     std::string_view("eEpP").contains(text.at(index - 1)))))
            {
                result += text.at(index++);
            }
        }
        else
        {
            result += text.at(index++);
        }
    }
    return result;
}

template <class Predicate>
[[nodiscard]] std::string ControlledDirective(std::string_view source, Predicate controlled)
{
    const auto normalized = DirectiveText(source);
    auto remaining = std::string_view(normalized);
    while (!remaining.empty())
    {
        const auto end = remaining.find('\n');
        auto row = TrimDirectiveSpace(remaining.substr(0, end));
        if (row.starts_with('#') || row.starts_with("%:"))
        {
            row = TrimDirectiveSpace(row.substr(row.starts_with('#') ? 1 : 2));
            std::size_t count = 0;
            while (count < row.size() && NameCharacter(row.at(count)))
            {
                ++count;
            }
            const auto directive = row.substr(0, count);
            if (directive == "define" || directive == "undef")
            {
                row = TrimDirectiveSpace(row.substr(count));
                count = 0;
                while (count < row.size() && NameCharacter(row.at(count)))
                {
                    ++count;
                }
                const auto name = row.substr(0, count);
                if (controlled(name))
                {
                    return std::string(name);
                }
            }
        }
        remaining = end == std::string_view::npos ? std::string_view{} : remaining.substr(end + 1);
    }
    return {};
}

[[nodiscard]] std::map<std::string, std::string> LibcxxConfiguration(std::string_view text)
{
    std::map<std::string, std::string> values;
    while (!text.empty())
    {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        // A CR paired with LF is a line terminator, not macro replacement text.
        if (end != std::string_view::npos && line.ends_with('\r'))
        {
            line.remove_suffix(1);
        }
        if (line.starts_with("#define _LIBCPP_"))
        {
            const auto definition = line.substr(8);
            const auto name = definition.substr(0, definition.find_first_of(" \t("));
            // The entire ABI family is deliberate: the installed releases add
            // switches over time, including valueless bounded-iterator defines.
            if (ControlledLibcxxMacro(name))
            {
                values.emplace(std::string(name), std::string(definition));
            }
        }
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    }
    return values;
}
} // namespace

Status CheckLibcxxConfiguration(std::string_view actual, std::string_view baseline, FailureReport& report)
{
    const auto expected = LibcxxConfiguration(baseline);
    auto observed = LibcxxConfiguration(actual);
    // Implementation headers also declare local feature helpers (algorithm
    // vectorization and ASan container checks). Every configuration key from
    // __config remains mandatory. Additional ABI switches always conflict;
    // source definitions in all controlled families are checked in physical files.
    std::erase_if(observed, [&](const auto& value)
                  { return !value.first.starts_with("_LIBCPP_ABI_") && !expected.contains(value.first); });
    if (expected.empty() || observed != expected)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "libcxx_configuration_conflict"));
    }
    return {};
}

Status CheckLibcxxSourceConfiguration(const std::filesystem::path& source, std::span<const ClangHeader> observed,
                                      const std::filesystem::path& headers, const std::filesystem::path& site,
                                      FailureReport& report)
{
    std::error_code error;
    std::set<std::filesystem::path> files{std::filesystem::canonical(source, error)};
    if (error)
    {
        return std::unexpected(
            report.Add(ToolError::kReadInput, "libcxx_physical_source", source.generic_string(), error));
    }
    for (const auto& header : observed)
    {
        files.insert(header.Physical);
    }
    for (const auto& file : files)
    {
        const auto relative = file.lexically_relative(headers);
        if (file == site || (!relative.empty() && *relative.begin() != ".."))
        {
            continue;
        }
        const auto text = ReadFile(file, "libcxx_physical_directives", report);
        if (!text)
        {
            return std::unexpected(text.error());
        }
        const auto macro = ControlledDirective(*text, ControlledLibcxxMacro);
        if (!macro.empty())
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "libcxx_source_configuration",
                                              macro + " in " + file.generic_string()));
        }
    }
    return {};
}

Status CheckWindowsSourceConfiguration(const std::filesystem::path& source, std::span<const ClangHeader> observed,
                                       std::span<const std::filesystem::path> trustedRoots, FailureReport& report)
{
    std::error_code error;
    const auto original = std::filesystem::canonical(source, error);
    if (error)
    {
        return std::unexpected(
            report.Add(ToolError::kReadInput, "windows_physical_source", source.generic_string(), error));
    }
    std::vector<std::filesystem::path> trusted;
    for (const auto& root : trustedRoots)
    {
        auto path = std::filesystem::canonical(root, error);
        if (error)
        {
            return std::unexpected(
                report.Add(ToolError::kReadInput, "windows_physical_sdk", root.generic_string(), error));
        }
        trusted.push_back(std::move(path));
    }
    std::set<std::filesystem::path> files{original};
    for (const auto& header : observed)
    {
        files.insert(header.Physical);
    }
    for (const auto& file : files)
    {
        if (file != original &&
            std::ranges::any_of(trusted,
                                [&](const auto& root)
                                {
#ifdef _WIN32
                                    auto normalizedFile = file.wstring();
                                    auto normalizedRoot = root.wstring();
                                    const auto lower = [](wchar_t value) { return std::towlower(value); };
                                    std::ranges::transform(normalizedFile, normalizedFile.begin(), lower);
                                    std::ranges::transform(normalizedRoot, normalizedRoot.begin(), lower);
                                    const auto relative =
                                        std::filesystem::path(normalizedFile).lexically_relative(normalizedRoot);
#else
            const auto relative = file.lexically_relative(root);
#endif
                                    return !relative.empty() && *relative.begin() != "..";
                                }))
        {
            continue;
        }
        const auto text = ReadFile(file, "windows_physical_directives", report);
        if (!text)
        {
            return std::unexpected(text.error());
        }
        const auto macro = ControlledDirective(
            *text,
            [](std::string_view name)
            {
                return name.starts_with("_HAS_") || name.starts_with("_MSVC_STL_") || name == "_ITERATOR_DEBUG_LEVEL" ||
                       name == "_SECURE_SCL" || name == "_CRT_PACKING" || name == "_MT" || name == "_DLL" ||
                       name == "_DEBUG" || name.starts_with("_ALLOW_ITERATOR_DEBUG_LEVEL_") ||
                       name == "_ALLOW_RUNTIME_LIBRARY_MISMATCH" || name == "_ALLOW_MSC_VER_MISMATCH" ||
                       name == "_ENFORCE_MATCHING_ALLOCATORS" || name == "_ENABLE_EXTENDED_ALIGNED_STORAGE" ||
                       name == "_DISABLE_EXTENDED_ALIGNED_STORAGE" || name == "_USE_32BIT_TIME_T";
            });
        if (!macro.empty())
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_source_configuration",
                                              macro + " in " + file.generic_string()));
        }
    }
    return {};
}

Status CheckLibcxxDriverArguments(std::span<const std::string> arguments, FailureReport& report)
{
    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
        const auto& arg = arguments.subspan(index).front();
        if (arg == "-D" || arg == "-U")
        {
            ++index;
            if (index == arguments.size() || arguments.subspan(index).front().starts_with("_LIBCPP_"))
            {
                return std::unexpected(report.Add(ToolError::kInvalidProfile, "libcxx_abi_option", arg));
            }
            continue;
        }
        if (arg.starts_with("-D_LIBCPP_") || arg.starts_with("-U_LIBCPP_") || arg.starts_with("-X") ||
            arg.starts_with("-Wp,") || arg == "-mllvm" || arg.starts_with("-cc1") || arg.starts_with("-B") ||
            arg.starts_with("-resource-dir") || arg.starts_with("-include-pch") ||
            (arg.starts_with("--") && !arg.starts_with("--target=") && !arg.starts_with("--sysroot=")))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "libcxx_abi_option", arg));
        }
        // Fail closed for code-generation families. Ordinary project defines,
        // includes, warnings, optimization and debugging flags remain usable.
        if (arg.starts_with("-f"))
        {
            if (arg == "-fPIC" || arg == "-fPIE" || arg == "-fpic" || arg == "-fpie" || arg == "-fno-exceptions" ||
                arg == "-frtti" || arg == "-fno-rtti" || arg == "-fvisibility=hidden" ||
                arg == "-fvisibility=default" || arg == "-fvisibility-inlines-hidden" || arg == "-fdata-sections" ||
                arg == "-ffunction-sections" || arg == "-funwind-tables" || arg == "-fasynchronous-unwind-tables" ||
                arg == "-fstack-protector" || arg == "-fstack-protector-strong" || arg == "-fstack-protector-all" ||
                arg == "-fno-stack-protector" || arg == "-fno-omit-frame-pointer" || arg == "-fomit-frame-pointer" ||
                arg == "-fno-optimize-sibling-calls" || arg == "-fcolor-diagnostics" ||
                arg == "-fno-color-diagnostics" || arg.starts_with("-fdiagnostics-") ||
                arg.starts_with("-fdebug-prefix-map=") || arg.starts_with("-ffile-prefix-map=") ||
                arg.starts_with("-fmacro-prefix-map="))
            {
                continue;
            }
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "libcxx_unsupported_codegen_option", arg));
        }
        if (arg.starts_with("-m") && !arg.starts_with("-mmacosx-version-min=") &&
            !arg.starts_with("-miphoneos-version-min="))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "libcxx_unsupported_machine_option", arg));
        }
    }
    return {};
}

Status CheckClangHeaderOrigins(std::span<const ClangHeader> headers, const std::filesystem::path& root,
                               std::span<const std::string_view> keys, FailureReport& report)
{
    for (const auto key : keys)
    {
        std::error_code error;
        const auto expected = std::filesystem::canonical(root / key, error);
        bool found = false;
        if (error)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "clang_header_expected_missing", key, error));
        }
        for (const auto& path : headers)
        {
            if (path.Physical == expected)
            {
                found = true;
            }
            if (path.Spelling.generic_string().ends_with("/" + std::string(key)) && path.Physical != expected)
            {
                return std::unexpected(report.Add(ToolError::kInvalidProfile, "clang_header_origin_conflict",
                                                  path.Spelling.generic_string()));
            }
        }
        if (!found)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "clang_header_origin_missing", key));
        }
    }
    return {};
}

Result<std::vector<ClangHeader>>
ReadClangHeaderTrace(std::string_view trace, const std::filesystem::path& compilerDirectory, FailureReport& report)
{
    std::vector<ClangHeader> headers;
    for (std::size_t cursor = 0; cursor < trace.size();)
    {
        const auto end = trace.find('\n', cursor);
        auto row = trace.substr(cursor, end == std::string_view::npos ? end : end - cursor);
        if (row.ends_with('\r'))
        {
            row.remove_suffix(1);
        }
        // Other driver diagnostics may appear in stderr. A Clang -H record
        // starts with 1+ dots and a single separating space.
        const auto count = row.find_first_not_of('.');
        if (!row.empty() && row.front() == '.' && count != std::string_view::npos && row.at(count) == ' ')
        {
            const auto name = row.substr(count + 1);
            if (name.empty() || name.contains('\0'))
            {
                return std::unexpected(report.Add(ToolError::kInvalidProfile, "clang_header_trace_invalid"));
            }
            auto path = std::filesystem::path(std::u8string(name.begin(), name.end()));
            if (!path.is_absolute())
            {
                path = compilerDirectory / path;
            }
            std::error_code error;
            auto canonical = std::filesystem::canonical(path, error);
            if (error || !std::filesystem::is_regular_file(canonical, error) || error)
            {
                return std::unexpected(
                    report.Add(ToolError::kInvalidProfile, "clang_header_trace_missing", path.generic_string(), error));
            }
            headers.push_back({.Spelling = std::move(path), .Physical = std::move(canonical)});
        }
        if (end == std::string_view::npos)
        {
            break;
        }
        cursor = end + 1;
    }
    if (headers.empty())
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "clang_header_trace_empty"));
    }
    return headers;
}
Result<std::vector<std::filesystem::path>>
ParseClangHeaderTrace(std::string_view trace, const std::filesystem::path& compilerDirectory, FailureReport& report)
{
    const auto records = ReadClangHeaderTrace(trace, compilerDirectory, report);
    if (!records)
    {
        return std::unexpected(records.error());
    }
    std::vector<std::filesystem::path> files;
    for (const auto& record : *records)
    {
        if (std::ranges::find(files, record.Physical) == files.end())
        {
            files.push_back(record.Physical);
        }
    }
    return files;
}
} // namespace pg::abi
