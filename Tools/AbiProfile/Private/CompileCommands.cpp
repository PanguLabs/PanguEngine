#include "CompileCommands.h"
#include "CollectionRequest.h"
#include "JsonSchema.h"
#include "ToolIo.h"
#include "ToolReport.h"
#include <Error.h>
#include <Result.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h> // IWYU pragma: keep - Windows SDK prerequisites.
#include <minwindef.h>
#include <stringapiset.h>
#include <winnls.h>
#endif

namespace pg::abi
{
namespace
{
[[nodiscard]] Error Reject(FailureReport& report, std::string_view stage, std::string_view detail = {})
{
    return report.Add(ToolError::kInvalidRequest, stage, detail);
}

[[nodiscard]] std::filesystem::path Path(std::string_view text) { return {std::u8string(text.begin(), text.end())}; }

[[nodiscard]] Result<std::filesystem::path> Resolve(const std::filesystem::path& path,
                                                    const std::filesystem::path& directory, FailureReport& report)
{
    std::error_code error;
    auto result = std::filesystem::weakly_canonical(path.is_absolute() ? path : directory / path, error);
    if (error || !result.is_absolute())
    {
        return std::unexpected(report.Add(ToolError::kResolvePath, "compile_path", path.generic_string(), error));
    }
    return result;
}

[[nodiscard]] bool Same(const std::filesystem::path& left, const std::filesystem::path& right, Platform host)
{
#ifdef _WIN32
    if (host == Platform::kWindows)
    {
        const auto first = left.generic_wstring();
        const auto second = right.generic_wstring();
        return ::CompareStringOrdinal(first.c_str(), -1, second.c_str(), -1, TRUE) == CSTR_EQUAL;
    }
#else
    (void)host;
#endif
    return left == right;
}

[[nodiscard]] bool Space(char value) { return value == ' ' || value == '\t' || value == '\r' || value == '\n'; }

[[nodiscard]] Result<std::vector<std::string>> ParseWindowsArguments(std::string_view text, bool strictQuotes,
                                                                     FailureReport& report)
{
    std::vector<std::string> result;
    std::string current;
    bool quoted = false;
    bool started = false;
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        if (!quoted && Space(text.at(index)))
        {
            if (started)
            {
                result.push_back(std::move(current));
                current.clear();
                started = false;
            }
            continue;
        }
        started = true;
        if (text.at(index) == '\\')
        {
            const auto begin = index;
            while (index < text.size() && text.at(index) == '\\')
            {
                ++index;
            }
            const auto count = index - begin;
            if (index < text.size() && text.at(index) == '"')
            {
                current.append(count / 2, '\\');
                if (count % 2 != 0)
                {
                    current.push_back('"');
                    continue;
                }
            }
            else
            {
                current.append(count, '\\');
                --index;
                continue;
            }
        }
        if (text.at(index) == '"')
        {
            if (quoted && index + 1 < text.size() && text.at(index + 1) == '"')
            {
                current.push_back('"');
                ++index;
            }
            else
            {
                quoted = !quoted;
            }
        }
        else
        {
            current.push_back(text.at(index));
        }
    }
    if (strictQuotes && quoted)
    {
        return std::unexpected(Reject(report, "command_quote"));
    }
    if (started)
    {
        result.push_back(std::move(current));
    }
    return result;
}

[[nodiscard]] Result<std::vector<std::string>> ParseResponseArguments(std::string_view text, ResponseFileStyle style,
                                                                      FailureReport& report)
{
    if (text.contains('\0'))
    {
        return std::unexpected(Reject(report, "response_nul"));
    }
    if (style == ResponseFileStyle::kWindows)
    {
        return ParseWindowsArguments(text, false, report);
    }
    // LLVM TokenizeGNUCommandLine escapes the next byte in both quote kinds;
    // shell POSIX quoting deliberately has different rules. Unterminated quotes
    // and a final backslash are accepted by the actual response tokenizer.
    std::vector<std::string> result;
    std::string token;
    bool started = false;
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        const auto value = text.at(index);
        if (!started && Space(value))
        {
            continue;
        }
        started = true;
        if (value == '\\' && index + 1 < text.size())
        {
            token.push_back(text.at(++index));
        }
        else if (value == '"' || value == '\'')
        {
            ++index;
            while (index < text.size() && text.at(index) != value)
            {
                if (text.at(index) == '\\' && index + 1 < text.size())
                {
                    ++index;
                }
                token.push_back(text.at(index));
                ++index;
            }
            if (index == text.size())
            {
                break;
            }
        }
        else if (Space(value))
        {
            result.push_back(std::move(token));
            token.clear();
            started = false;
        }
        else
        {
            token.push_back(value);
        }
    }
    if (started)
    {
        result.push_back(std::move(token));
    }
    return result;
}

[[nodiscard]] std::string DriverName(std::string_view argv0, Platform host)
{
    auto name = Path(argv0).filename().string();
    if (host == Platform::kWindows)
    {
        std::ranges::transform(
            name, name.begin(), [](unsigned char value)
            { return static_cast<char>(value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value); });
    }
    return name;
}

[[nodiscard]] std::optional<std::string_view> DriverSuffix(std::string_view name)
{
    // Order follows Clang's FindDriverSuffix; a target prefix may precede it.
    for (const auto& [suffix, mode] : {
             std::pair{"clang", "gcc"},
             std::pair{"clang++", "g++"},
             std::pair{"clang-c++", "g++"},
             std::pair{"clang-cc", "gcc"},
             std::pair{"clang-cpp", "cpp"},
             std::pair{"clang-g++", "g++"},
             std::pair{"clang-gcc", "gcc"},
             std::pair{"clang-cl", "cl"},
             std::pair{"cc", "gcc"},
             std::pair{"cpp", "cpp"},
             std::pair{"cl", "cl"},
             std::pair{"++", "g++"},
             std::pair{"flang", "flang"},
             std::pair{"flang-new", "flang"},
             std::pair{"clang-dxc", "dxc"},
         })
    {
        if (name.ends_with(suffix))
        {
            return mode;
        }
    }
    return std::nullopt;
}

[[nodiscard]] Result<std::string> DriverMode(std::span<const std::string> arguments, Platform host,
                                             FailureReport& report)
{
    auto name = DriverName(arguments.front(), host);
    auto mode = DriverSuffix(name);
    if (!mode && name.ends_with(".exe"))
    {
        name.resize(name.size() - 4);
        mode = DriverSuffix(name);
    }
    if (!mode)
    {
        const auto last = name.find_last_not_of("0123456789.");
        name.resize(last == std::string::npos ? 0 : last + 1);
        mode = DriverSuffix(name);
    }
    if (!mode)
    {
        const auto dash = name.rfind('-');
        if (dash != std::string::npos)
        {
            name.resize(dash);
            mode = DriverSuffix(name);
        }
    }
    std::string result(mode.value_or("gcc"));
    for (const auto& argument : arguments.subspan(1))
    {
        if (argument.starts_with("--driver-mode="))
        {
            result = std::string_view(argument).substr(std::string_view("--driver-mode=").size());
        }
        else if (argument == "--driver-mode")
        {
            return std::unexpected(Reject(report, "compile_driver_mode", argument));
        }
    }
    if (result != "gcc" && result != "g++" && result != "cl")
    {
        return std::unexpected(Reject(report, "compile_driver_mode", result));
    }
    return result;
}

[[nodiscard]] Result<ResponseFileStyle> ResponseStyle(std::span<const std::string> original, bool clDriver,
                                                      FailureReport& report)
{
    auto style = clDriver ? ResponseFileStyle::kWindows : ResponseFileStyle::kGnu;
    // Clang chooses once, before any @ expansion; the last supported explicit
    // raw command-line quoting option wins. Options inside rsp do not retokenize.
    for (const auto& argument : original.subspan(1))
    {
        if (argument == "--rsp-quoting=posix")
        {
            style = ResponseFileStyle::kGnu;
        }
        else if (argument == "--rsp-quoting=windows")
        {
            style = ResponseFileStyle::kWindows;
        }
        else if (argument.starts_with("--rsp-quoting"))
        {
            return std::unexpected(Reject(report, "compile_rsp_quoting", argument));
        }
    }
    return style;
}

[[nodiscard]] Result<std::string> ReadResponse(const std::filesystem::path& path, std::size_t budget,
                                               FailureReport& report)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return std::unexpected(report.Add(ToolError::kReadInput, "response_read", path.generic_string()));
    }
    std::string contents;
    std::array<char, 4096> buffer{};
    while (file)
    {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = static_cast<std::size_t>(file.gcount());
        if (count > budget - contents.size())
        {
            return std::unexpected(Reject(report, "response_size", path.generic_string()));
        }
        contents.append(buffer.data(), count);
    }
    if (!file.eof())
    {
        return std::unexpected(report.Add(ToolError::kReadInput, "response_read", path.generic_string()));
    }
    file.clear();
    file.close();
    if (file.fail())
    {
        return std::unexpected(report.Add(ToolError::kCloseFile, "response_close", path.generic_string()));
    }
    return contents;
}

struct ResponseExpansion
{
    Platform Host;
    ResponseFileStyle Style;
    std::filesystem::path Directory;
    FailureReport* Report{};
    std::vector<std::filesystem::path> Active;
    std::vector<std::filesystem::path> Files;
    std::vector<std::string> Arguments;
    std::size_t Bytes{};

    // Explicit depth <= 8 and cumulative bytes <= 1 MiB bound this traversal.
    // NOLINTNEXTLINE(misc-no-recursion)
    [[nodiscard]] Status Expand(std::span<const std::string> arguments)
    {
        constexpr std::size_t kMaxBytes = std::size_t{1024} * 1024;
        constexpr std::size_t kMaxDepth = 8;
        for (const auto& arg : arguments)
        {
            if (!arg.starts_with('@'))
            {
                Arguments.push_back(arg);
                continue;
            }
            if (arg.size() == 1 || Active.size() == kMaxDepth)
            {
                return std::unexpected(Reject(*Report, "response_depth_or_path", arg));
            }
            // Clang resolves every nested response from the compiler cwd.
            const auto path = Resolve(Path(std::string_view(arg).substr(1)), Directory, *Report);
            if (!path)
            {
                return std::unexpected(path.error());
            }
            if (std::ranges::any_of(Active, [&](const auto& item) { return Same(item, *path, Host); }))
            {
                return std::unexpected(Reject(*Report, "response_cycle", arg));
            }
            const auto contents = ReadResponse(*path, kMaxBytes - Bytes, *Report);
            if (!contents)
            {
                return std::unexpected(contents.error());
            }
            if (contents->size() > kMaxBytes - Bytes)
            {
                return std::unexpected(Reject(*Report, "response_size", arg));
            }
            Bytes += contents->size();
            auto text = std::string_view(*contents);
            if (text.starts_with("\xEF\xBB\xBF"))
            {
                text.remove_prefix(3);
            }
            const auto parsed = ParseResponseArguments(text, Style, *Report);
            if (!parsed)
            {
                return std::unexpected(parsed.error());
            }
            Files.push_back(*path);
            Active.push_back(*path);
            const auto expanded = Expand(*parsed);
            Active.pop_back();
            if (!expanded)
            {
                return expanded;
            }
        }
        return {};
    }
};

[[nodiscard]] Result<std::vector<std::string>> ReadArguments(const Json& entry, CommandLineStyle style,
                                                             FailureReport& report)
{
    if (const auto* args = Find(entry, "arguments"); args != nullptr)
    {
        if (!args->is_array() || args->empty())
        {
            return std::unexpected(Reject(report, "compile_arguments"));
        }
        std::vector<std::string> result;
        for (const auto& arg : *args)
        {
            if (!arg.is_string() || Text(arg).contains('\0'))
            {
                return std::unexpected(Reject(report, "compile_arguments"));
            }
            result.emplace_back(Text(arg));
        }
        return result;
    }
    const auto* command = Find(entry, "command");
    if (command == nullptr || !command->is_string())
    {
        return std::unexpected(Reject(report, "compile_command"));
    }
    return ParseCommandLine(Text(*command), style, report);
}

[[nodiscard]] bool OperandOption(std::string_view arg)
{
    // Preserve option operands even when they happen to spell the source path.
    return arg == "-I" || arg == "-isystem" || arg == "-iquote" || arg == "-idirafter" || arg == "-include" ||
           arg == "-imacros" || arg == "-D" || arg == "-U" || arg == "-Xclang" || arg == "-Xpreprocessor" ||
           arg == "-target" || arg == "--target" || arg == "-isysroot" || arg == "--sysroot" || arg == "-x" ||
           arg == "-resource-dir" || arg == "-B";
}

[[nodiscard]] Result<CompileUnit> MakeUnit(const CollectionRequest& request, std::vector<std::string> original,
                                           const std::filesystem::path& directory, const std::filesystem::path& source,
                                           FailureReport& report)
{
    if (original.empty() || original.front().empty())
    {
        return std::unexpected(Reject(report, "compile_arguments"));
    }
    CompileUnit unit;
    unit.Directory = directory;
    unit.Source = source;
    unit.OriginalArguments = std::move(original);
    // argv[0] must be the compiler; never bless a wrapper's trailing compiler.
    const auto compiler = Resolve(Path(unit.OriginalArguments.front()), directory, report);
    const auto requestedCompiler = Resolve(request.Compiler, request.BuildRoot, report);
    if (!compiler || !requestedCompiler || !Same(*compiler, *requestedCompiler, request.HostPlatform))
    {
        return std::unexpected(Reject(report, "compile_compiler_identity", unit.OriginalArguments.front()));
    }
    unit.Compiler = *compiler;
    unit.DriverName = DriverName(unit.OriginalArguments.front(), request.HostPlatform);
    const auto initialMode = DriverMode(unit.OriginalArguments, request.HostPlatform, report);
    if (!initialMode)
    {
        return std::unexpected(initialMode.error());
    }
    const auto style = ResponseStyle(unit.OriginalArguments, *initialMode == "cl", report);
    if (!style)
    {
        return std::unexpected(style.error());
    }
    unit.ResponseStyle = *style;
    ResponseExpansion expansion{
        .Host = request.HostPlatform,
        .Style = unit.ResponseStyle,
        .Directory = directory,
        .Report = &report,
        .Active = {},
        .Files = {},
        .Arguments = {},
        .Bytes = 0,
    };
    unit.Arguments.push_back(unit.OriginalArguments.front());
    if (const auto expanded = expansion.Expand(std::span(unit.OriginalArguments).subspan(1)); !expanded)
    {
        return std::unexpected(expanded.error());
    }
    unit.Arguments.insert(unit.Arguments.end(), expansion.Arguments.begin(), expansion.Arguments.end());
    unit.ResponseFiles = std::move(expansion.Files);
    const auto effectiveMode = DriverMode(unit.Arguments, request.HostPlatform, report);
    if (!effectiveMode)
    {
        return std::unexpected(effectiveMode.error());
    }
    unit.DriverMode = *effectiveMode;
    const bool clDriver = unit.DriverMode == "cl";
    bool sawSource = false;
    bool sawCompile = false;
    bool positionalOnly = false;
    std::size_t sourcePosition = 0;
    for (std::size_t index = 1; index < unit.Arguments.size(); ++index)
    {
        const auto arg = std::string_view(unit.Arguments.at(index));
        if (!positionalOnly && arg == "--")
        {
            positionalOnly = true;
            unit.AbiArguments.emplace_back(arg);
            continue;
        }
        if (!positionalOnly && (arg == "-c" || arg == "/c"))
        {
            sawCompile = true;
            continue;
        }
        const bool outputPair = arg == "-o" || arg == "/Fo" || arg == "-Fo";
        const bool outputJoined = (arg.starts_with("-o") && arg.size() > 2) ||
                                  ((arg.starts_with("/Fo") || arg.starts_with("-Fo")) && arg.size() > 3);
        if (!positionalOnly && (outputPair || outputJoined))
        {
            auto value = outputJoined ? arg.substr(arg.starts_with("-o") ? 2 : 3) : std::string_view{};
            if (outputPair)
            {
                if (++index == unit.Arguments.size())
                {
                    return std::unexpected(Reject(report, "compile_output_operand"));
                }
                value = unit.Arguments.at(index);
            }
            if (!unit.Object.empty() || value.empty())
            {
                return std::unexpected(Reject(report, "compile_output_duplicate_or_empty"));
            }
            const auto object = Resolve(Path(value), directory, report);
            if (!object)
            {
                return std::unexpected(object.error());
            }
            unit.Object = *object;
            continue;
        }
        // Only understood Clang dependency emission controls may differ.
        if (!positionalOnly && !clDriver && (arg == "-MD" || arg == "-MMD" || arg == "-MP" || arg == "-MG"))
        {
            continue;
        }
        if (!positionalOnly && !clDriver && (arg == "-MF" || arg == "-MT" || arg == "-MQ"))
        {
            ++index;
            if (index == unit.Arguments.size() || unit.Arguments.at(index).empty())
            {
                return std::unexpected(Reject(report, "compile_dependency_operand"));
            }
            continue;
        }
        if (!positionalOnly && !clDriver &&
            (arg.starts_with("-MF") || arg.starts_with("-MT") || arg.starts_with("-MQ")) && arg.size() > 3)
        {
            continue;
        }
        if (!positionalOnly && OperandOption(arg))
        {
            unit.AbiArguments.emplace_back(arg);
            if (++index == unit.Arguments.size())
            {
                return std::unexpected(Reject(report, "compile_option_operand", arg));
            }
            unit.AbiArguments.push_back(unit.Arguments.at(index));
            continue;
        }
        if (!arg.empty() && (positionalOnly || !arg.starts_with('-')))
        {
            const auto input = Resolve(Path(arg), directory, report);
            if (!input)
            {
                return std::unexpected(input.error());
            }
            if (Same(*input, source, request.HostPlatform))
            {
                if (sawSource)
                {
                    return std::unexpected(Reject(report, "compile_source_duplicate"));
                }
                sawSource = true;
                sourcePosition = unit.AbiArguments.size();
                continue;
            }
        }
        unit.AbiArguments.emplace_back(arg);
    }
    if (unit.Object.empty() || !sawSource || !sawCompile)
    {
        return std::unexpected(Reject(report, "compile_source_or_output"));
    }
    unit.ComparisonArguments = unit.AbiArguments;
    const auto sourceText = unit.Source.generic_u8string();
    unit.ComparisonArguments.insert(unit.ComparisonArguments.begin() + static_cast<std::ptrdiff_t>(sourcePosition),
                                    std::string(sourceText.begin(), sourceText.end()));
    return unit;
}

[[nodiscard]] std::string_view StringField(const Json& value, std::string_view key)
{
    const auto* field = Find(value, key);
    return field != nullptr && field->is_string() ? Text(*field) : std::string_view{};
}
} // namespace

Result<std::vector<std::string>> ParseCommandLine(std::string_view text, CommandLineStyle style, FailureReport& report)
{
    if (text.contains('\0'))
    {
        return std::unexpected(Reject(report, "command_nul"));
    }
    if (style == CommandLineStyle::kWindows)
    {
        return ParseWindowsArguments(text, true, report);
    }
    std::vector<std::string> result;
    std::string current;
    char quote = '\0';
    bool started = false;
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        const auto value = text.at(index);
        if (quote == '\0' && Space(value))
        {
            if (started)
            {
                result.push_back(std::move(current));
                current.clear();
                started = false;
            }
            continue;
        }
        started = true;
        if (value == '\\' && quote != '\'')
        {
            if (++index == text.size())
            {
                return std::unexpected(Reject(report, "command_escape"));
            }
            const auto next = text.at(index);
            if (quote == '"' && next != '"' && next != '\\' && next != '$' && next != '`' && next != '\n')
            {
                current.push_back('\\');
            }
            if (next != '\n')
            {
                current.push_back(next);
            }
        }
        else if ((value == '\'' || value == '"') && (quote == '\0' || quote == value))
        {
            quote = quote == '\0' ? value : '\0';
        }
        else
        {
            current.push_back(value);
        }
    }
    if (quote != '\0')
    {
        return std::unexpected(Reject(report, "command_quote"));
    }
    if (started)
    {
        result.push_back(std::move(current));
    }
    return result;
}

Result<std::vector<CompileUnit>> LoadCompileUnits(const CollectionRequest& request, FailureReport& report)
{
    const auto manifest = ReadFile(request.ObjectManifest, "object_manifest", report);
    if (!manifest)
    {
        return std::unexpected(manifest.error());
    }
    std::vector<std::filesystem::path> objects;
    auto lines = std::string_view(*manifest);
    while (!lines.empty())
    {
        const auto end = lines.find('\n');
        auto line = lines.substr(0, end);
        if (line.ends_with('\r'))
        {
            line.remove_suffix(1);
        }
        if (!line.empty())
        {
            if (line.contains('\0'))
            {
                return std::unexpected(Reject(report, "object_manifest_nul"));
            }
            const auto object = Resolve(Path(line), request.BuildRoot, report);
            if (!object)
            {
                return std::unexpected(object.error());
            }
            if (std::ranges::any_of(objects,
                                    [&](const auto& item) { return Same(item, *object, request.HostPlatform); }))
            {
                return std::unexpected(Reject(report, "object_manifest_duplicate", line));
            }
            objects.push_back(*object);
        }
        lines = end == std::string_view::npos ? std::string_view{} : lines.substr(end + 1);
    }
    if (objects.empty())
    {
        return std::unexpected(Reject(report, "object_manifest_empty"));
    }
    const auto contents = ReadFile(request.Database, "compile_database", report);
    if (!contents)
    {
        return std::unexpected(contents.error());
    }
    const auto database = ParseUnique(*contents);
    if (!database || !database->is_array())
    {
        return std::unexpected(Reject(report, "compile_database_json"));
    }
    std::vector<std::optional<CompileUnit>> selected(objects.size());
    for (const auto& entry : *database)
    {
        const auto directoryText = StringField(entry, "directory");
        const auto sourceText = StringField(entry, "file");
        if (directoryText.empty() || sourceText.empty() || directoryText.contains('\0') || sourceText.contains('\0'))
        {
            return std::unexpected(Reject(report, "compile_database_entry"));
        }
        const auto directory = Resolve(Path(directoryText), request.BuildRoot, report);
        if (!directory)
        {
            return std::unexpected(directory.error());
        }
        std::optional<std::filesystem::path> declared;
        if (const auto* output = Find(entry, "output"); output != nullptr)
        {
            if (!output->is_string() || Text(*output).empty() || Text(*output).contains('\0'))
            {
                return std::unexpected(Reject(report, "compile_database_output"));
            }
            const auto resolved = Resolve(Path(Text(*output)), *directory, report);
            if (!resolved)
            {
                return std::unexpected(resolved.error());
            }
            declared = *resolved;
            if (!std::ranges::any_of(objects,
                                     [&](const auto& object) { return Same(object, *declared, request.HostPlatform); }))
            {
                continue;
            }
        }
        auto arguments = ReadArguments(entry, request.CommandLineStyle, report);
        const auto source = Resolve(Path(sourceText), *directory, report);
        if (!arguments || !source)
        {
            return std::unexpected(!arguments ? arguments.error() : source.error());
        }
        auto unit = MakeUnit(request, std::move(*arguments), *directory, *source, report);
        if (!unit)
        {
            return std::unexpected(unit.error());
        }
        if (declared && !Same(unit->Object, *declared, request.HostPlatform))
        {
            return std::unexpected(Reject(report, "compile_output_disagreement"));
        }
        for (std::size_t index = 0; index < objects.size(); ++index)
        {
            if (Same(objects.at(index), unit->Object, request.HostPlatform))
            {
                if (selected.at(index))
                {
                    return std::unexpected(Reject(report, "compile_output_duplicate"));
                }
                selected.at(index) = std::move(*unit);
                break;
            }
        }
    }
    std::vector<CompileUnit> units;
    for (auto& unit : selected)
    {
        if (!unit)
        {
            return std::unexpected(Reject(report, "compile_output_missing"));
        }
        units.push_back(std::move(*unit));
    }
    return units;
}

Result<CompileUnit> MatchCompilerInvocation(const CollectionRequest& request, std::span<const std::string> arguments,
                                            const std::filesystem::path& directory, FailureReport& report)
{
    const auto units = LoadCompileUnits(request, report);
    if (!units)
    {
        return std::unexpected(units.error());
    }
    const auto actualDirectory = Resolve(directory, request.BuildRoot, report);
    if (!actualDirectory)
    {
        return std::unexpected(actualDirectory.error());
    }
    for (const auto& expected : *units)
    {
        if (!Same(expected.Directory, *actualDirectory, request.HostPlatform))
        {
            continue;
        }
        FailureReport candidateReport;
        auto actual =
            MakeUnit(request, {arguments.begin(), arguments.end()}, *actualDirectory, expected.Source, candidateReport);
        if (actual && Same(actual->Object, expected.Object, request.HostPlatform) &&
            actual->DriverName == expected.DriverName && actual->DriverMode == expected.DriverMode &&
            actual->ComparisonArguments == expected.ComparisonArguments)
        {
            return actual;
        }
    }
    return std::unexpected(Reject(report, "compile_invocation_mismatch"));
}
} // namespace pg::abi
