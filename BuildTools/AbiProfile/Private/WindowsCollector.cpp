#include "WindowsCollector.h"

#include "CollectionRequest.h"
#include "CollectionState.h"
#include "CompileCommands.h"
#include "HeaderTrace.h"
#include "JsonSchema.h"
#include "Observation.h"
#include "Process.h"
#include "ToolIo.h"
#include "ToolReport.h"
#include "WindowsProfile.h"
#include <array>

#include <Result.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <expected>
#include <filesystem>
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
[[nodiscard]] Result<ProcessResult> Execute(const std::filesystem::path& program, const std::filesystem::path& working,
                                            std::vector<std::string> args, FailureReport& report)
{
    return ExecuteObservation(program, working, std::move(args), "windows_probe_failed", report);
}

[[nodiscard]] std::string Normalize(const std::filesystem::path& value)
{
    std::string result = value.lexically_normal().generic_string();
    std::ranges::transform(result, result.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return result;
}

[[nodiscard]] std::filesystem::path Path(std::string_view text) { return {std::u8string(text.begin(), text.end())}; }

// Probe writes are never allowed to follow an existing symlink or overwrite a
// hard-linked source/SDK file. Only private regular files may be reused.

[[nodiscard]] Result<std::string> RequireFile(const std::filesystem::path& path, FailureReport& report)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error)
    {
        return std::unexpected(
            report.Add(ToolError::kReadInput, "windows_required_file", path.generic_string(), error));
    }
    return ReadFile(path, "windows_read_metadata", report);
}

[[nodiscard]] std::string Trim(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
    {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

[[nodiscard]] Result<std::vector<std::string>> Headers(std::string_view stderrText,
                                                       const std::filesystem::path& working, FailureReport& report)
{
    std::vector<std::string> result;
    for (std::size_t start = 0; start < stderrText.size();)
    {
        const auto end = stderrText.find('\n', start);
        const auto line = Trim(stderrText.substr(start, end == std::string_view::npos ? end : end - start));
        std::size_t dots = 0;
        while (dots < line.size() && line.at(dots) == '.')
        {
            ++dots;
        }
        if (dots > 0 && dots < line.size() && line.at(dots) == ' ')
        {
            auto absolute = Path(line.substr(dots + 1));
            if (!absolute.is_absolute())
            {
                absolute = working / absolute;
            }
            const auto normalized = Normalize(absolute);
            if (!std::filesystem::exists(absolute))
            {
                return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_missing_include", normalized));
            }
            result.emplace_back(normalized);
        }
        if (end == std::string_view::npos)
        {
            break;
        }
        start = end + 1;
    }
    return result;
}

[[nodiscard]] Result<std::string> HeaderBySuffix(const std::vector<std::string>& paths, std::string_view suffix,
                                                 FailureReport& report)
{
    std::string found;
    for (const auto& header : paths)
    {
        if (header.ends_with(suffix))
        {
            if (!found.empty() && header != found)
            {
                return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_ambiguous_header", suffix));
            }
            found = header;
        }
    }
    if (found.empty())
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_missing_header", suffix));
    }
    return found;
}

[[nodiscard]] std::string FrontendTriple(std::string_view driver)
{
    const auto first = driver.find(R"("-triple" ")");
    if (first == std::string_view::npos)
    {
        return {};
    }
    const auto start = first + std::string_view(R"("-triple" ")").size();
    const auto end = driver.find('"', start);
    return end == std::string_view::npos ? std::string{} : std::string(driver.substr(start, end - start));
}

[[nodiscard]] Result<std::string> SdkVersion(const std::string& sdkHeader, FailureReport& report)
{
    const auto marker = std::string("/include/");
    const auto begin = sdkHeader.find(marker);
    if (begin == std::string::npos)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_sdk_path", sdkHeader));
    }
    const auto offset = begin + marker.size();
    const auto end = sdkHeader.find('/', offset);
    if (end == std::string::npos || end == offset)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_sdk_path", sdkHeader));
    }
    return sdkHeader.substr(offset, end - offset);
}

[[nodiscard]] Result<std::string> ToolsetVersion(const std::string& stlHeader, FailureReport& report)
{
    constexpr std::string_view kMarker = "/tools/msvc/";
    const auto start = stlHeader.find(kMarker);
    if (start == std::string_view::npos)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_toolset_path", stlHeader));
    }
    const auto offset = start + kMarker.size();
    const auto end = stlHeader.find('/', offset);
    if (end == std::string_view::npos || end == offset)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_toolset_path", stlHeader));
    }
    return stlHeader.substr(offset, end - offset);
}

[[nodiscard]] Result<std::string> Metadata(const std::filesystem::path& path, FailureReport& report)
{
    const auto contents = RequireFile(path, report);
    return contents ? Result<std::string>{Trim(*contents)} : std::unexpected(contents.error());
}

[[nodiscard]] Result<CompileObservation> Fail(FailureReport& report, std::string_view stage)
{
    return std::unexpected(report.Add(ToolError::kInvalidProfile, stage));
}
} // namespace

bool WindowsSdkManifestMatches(std::string_view xml, std::string_view expectedVersion)
{
    // Do not search arbitrary XML text: a comment or nested element can forge a
    // matching version while the real ApplicationPlatform root differs.
    constexpr std::string_view kElement = "<ApplicationPlatform";
    constexpr std::string_view kClosing = "</ApplicationPlatform>";
    const auto whitespace = [](char ch) noexcept { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; };
    const auto skipWhitespace = [&](std::size_t& position)
    {
        while (position < xml.size() && whitespace(xml.at(position)))
        {
            ++position;
        }
    };
    if (expectedVersion.empty() || xml.empty())
    {
        return false;
    }
    std::size_t cursor = 0;
    if (xml.starts_with("\xEF\xBB\xBF"))
    {
        cursor = 3;
    }
    skipWhitespace(cursor);
    if (xml.substr(cursor).starts_with("<?xml"))
    {
        const auto declarationEnd = xml.find("?>", cursor);
        if (declarationEnd == std::string_view::npos)
        {
            return false;
        }
        cursor = declarationEnd + 2;
        skipWhitespace(cursor);
    }
    if (!xml.substr(cursor).starts_with(kElement))
    {
        return false;
    }
    cursor += kElement.size();
    if (cursor == xml.size() || !whitespace(xml.at(cursor)))
    {
        return false;
    }
    bool seenVersion = false;
    bool selfClosing = false;
    for (;;)
    {
        skipWhitespace(cursor);
        if (cursor >= xml.size())
        {
            return false;
        }
        if (xml.at(cursor) == '>')
        {
            ++cursor;
            break;
        }
        if (xml.at(cursor) == '/' && cursor + 1 < xml.size() && xml.at(cursor + 1) == '>')
        {
            cursor += 2;
            selfClosing = true;
            break;
        }
        const auto nameStart = cursor;
        while (cursor < xml.size() && !whitespace(xml.at(cursor)) && xml.at(cursor) != '=' && xml.at(cursor) != '/' &&
               xml.at(cursor) != '>')
        {
            ++cursor;
        }
        if (cursor == nameStart)
        {
            return false;
        }
        const auto name = xml.substr(nameStart, cursor - nameStart);
        skipWhitespace(cursor);
        if (cursor >= xml.size() || xml.at(cursor) != '=')
        {
            return false;
        }
        ++cursor;
        skipWhitespace(cursor);
        if (cursor >= xml.size() || (xml.at(cursor) != '"' && xml.at(cursor) != '\''))
        {
            return false;
        }
        const auto quote = xml.at(cursor++);
        const auto valueStart = cursor;
        const auto valueEnd = xml.find(quote, cursor);
        if (valueEnd == std::string_view::npos)
        {
            return false;
        }
        if (name == "version")
        {
            if (seenVersion || xml.substr(valueStart, valueEnd - valueStart) != expectedVersion)
            {
                return false;
            }
            seenVersion = true;
        }
        cursor = valueEnd + 1;
    }
    if (!seenVersion || xml.find(kElement, cursor) != std::string_view::npos)
    {
        return false;
    }
    if (selfClosing)
    {
        skipWhitespace(cursor);
        return cursor == xml.size();
    }
    const auto closingStart = xml.rfind(kClosing);
    if (closingStart == std::string_view::npos || closingStart < cursor)
    {
        return false;
    }
    cursor = closingStart + kClosing.size();
    skipWhitespace(cursor);
    return cursor == xml.size();
}
Result<CompileObservation> InspectWindowsCompileInputs(const CollectionRequest& request,
                                                       std::span<const CompileUnit> units, FailureReport& report)
{
#ifndef _WIN32
    (void)request;
    (void)units;
    return Fail(report, "windows_requires_native_host");
#else
    if (request.HostPlatform != Platform::kWindows || request.TargetPlatform != Platform::kWindows || units.empty())
    {
        return Fail(report, "windows_platform_or_units");
    }
    const auto readobj = request.Compiler.parent_path() / "llvm-readobj.exe";
    const auto lld = request.Compiler.parent_path() / "lld-link.exe";
    if (!RequireFile(readobj, report) || !RequireFile(lld, report))
    {
        return Fail(report, "windows_llvm_companions");
    }
    const auto banner = Execute(request.Compiler, request.BuildRoot, {"--version"}, report);
    if (!banner)
    {
        return std::unexpected(banner.error());
    }
    const auto resourceDir = Execute(request.Compiler, request.BuildRoot, {"--print-resource-dir"}, report);
    if (!resourceDir)
    {
        return std::unexpected(resourceDir.error());
    }
    const auto probe = request.SourceRoot / "BuildTools/AbiProfile/Probe/Windows.cpp";
    const auto witness = request.SourceRoot / "BuildTools/AbiProfile/Probe/RuntimeWitness.cpp";
    const std::array knownInputs{probe, witness};
    const auto ownership = ObservationWork::Acquire(request, units, knownInputs, "windows_probe_failed", report);
    if (!ownership)
    {
        return std::unexpected(ownership.error());
    }
    const auto work = (*ownership)->Directory();
    if (!RequireFile(probe, report))
    {
        return Fail(report, "windows_probe_source");
    }
    CompileObservation observation;
    observation.ObservedInputs.push_back(probe);
    Json evidence = {
        {"schema", 1},
        {"target", request.TargetName},
        {"compiler", request.Compiler.generic_string()},
        {"compiler_banner", banner->Stdout},
        {"units", Json::array()},
    };
    std::string stlHeader;
    std::string crtHeader;
    std::string sdkHeader;
    std::string targetTriple;
    std::vector<std::string> witnessFlags;
    std::filesystem::path witnessDirectory;
    for (std::size_t index = 0; index < units.size(); ++index)
    {
        const auto& unit = units.subspan(index).front();
        // Include actual TU rather than a synthetic facsimile. The probe appends
        // SDK and STL checks without touching the target source tree.
        const auto observed = ObserveTranslationUnit(request, unit, probe, work, index, "windows_probe_failed", report);
        if (!observed)
        {
            return std::unexpected(observed.error());
        }
        const auto* macros = &observed->Macros;
        const auto* driver = &observed->Driver;
        const auto& flags = unit.AbiArguments;
        auto headers = Headers(macros->Stderr, unit.Directory, report);
        if (!headers)
        {
            return std::unexpected(headers.error());
        }
        const auto clangResource = Normalize(Path(Trim(resourceDir->Stdout)) / "include/yvals_core.h");
        std::erase(*headers, clangResource);
        const auto stl = HeaderBySuffix(*headers, "/yvals_core.h", report);
        const auto crt = HeaderBySuffix(*headers, "/ucrt/corecrt.h", report);
        const auto sdk = HeaderBySuffix(*headers, "/shared/sdkddkver.h", report);
        if (!stl || !crt || !sdk)
        {
            return Fail(report, "windows_required_headers");
        }
        const auto physicalHeaders = ReadClangHeaderTrace(macros->Stderr, unit.Directory, report);
        const auto sdkInclude = Path(*crt).parent_path().parent_path();
        const std::array trustedRoots{Path(*stl).parent_path(), sdkInclude};
        if (!physicalHeaders || !CheckWindowsSourceConfiguration(unit.Source, *physicalHeaders, trustedRoots, report))
        {
            return Fail(report, "windows_source_abi_controls");
        }
        const auto triple = FrontendTriple(driver->Stderr);
        if (triple.empty() ||
            (index != 0 && (triple != targetTriple || *stl != stlHeader || *crt != crtHeader || *sdk != sdkHeader)))
        {
            return Fail(report, "windows_tu_header_or_target_disagreement");
        }
        if (index == 0)
        {
            stlHeader = *stl;
            crtHeader = *crt;
            sdkHeader = *sdk;
            targetTriple = triple;
            witnessFlags = flags;
            witnessDirectory = unit.Directory;
        }
        evidence.at("units").push_back({
            {"source", unit.Source.generic_string()},
            {"object", unit.Object.generic_string()},
            {"macros", macros->Stdout},
            {"driver", driver->Stderr},
            {"headers", macros->Stderr},
            {"directives", ""},
            {"arguments", flags},
        });
        for (const auto& header : *headers)
        {
            // All headers are actual compiler inputs. Collect source and direct
            // SDK/toolset roots now; full depfile provenance is Task 11.
            observation.Headers.push_back(Path(header));
        }
    }
    const auto sdkVersion = SdkVersion(crtHeader, report);
    const auto toolsetVersion = ToolsetVersion(stlHeader, report);
    if (!sdkVersion || !toolsetVersion)
    {
        return Fail(report, "windows_metadata_version");
    }
    const auto vcPos = stlHeader.find("/tools/msvc/");
    const auto vcRoot = Path(stlHeader.substr(0, vcPos));
    const auto toolsetMeta = vcRoot / "auxiliary/build/Microsoft.VCToolsVersion.default.txt";
    const auto selectedToolset = Metadata(toolsetMeta, report);
    if (!selectedToolset || *selectedToolset != *toolsetVersion)
    {
        return Fail(report, "windows_toolset_metadata");
    }
    const auto sdkPos = crtHeader.find("/include/");
    const auto sdkRoot = Path(crtHeader.substr(0, sdkPos));
    const auto sdkManifest = sdkRoot / "platforms/uap" / *sdkVersion / "platform.xml";
    const auto platformXml = RequireFile(sdkManifest, report);
    if (!platformXml || !WindowsSdkManifestMatches(*platformXml, *sdkVersion))
    {
        return Fail(report, "windows_sdk_manifest");
    }
    const auto sdkSharedExpected = Normalize(sdkRoot / "include" / *sdkVersion / "shared/sdkddkver.h");
    if (sdkHeader != sdkSharedExpected)
    {
        return Fail(report, "windows_sdk_header_mismatch");
    }
    observation.ObservedInputs.push_back(toolsetMeta);
    observation.ObservedInputs.push_back(sdkManifest);
    evidence.update(Json{
        {"triple", targetTriple},
        {"stl_header", stlHeader},
        {"sdk_manifest", Normalize(sdkManifest)},
        {"sdk_version", *sdkVersion},
        {"toolset_version", *toolsetVersion},
        {"sdk_headers", Json::array({crtHeader, sdkHeader})},
    });
    // The link witness is independent from the Module objects, because STATIC
    // objects may legitimately defer unresolved symbols to the final consumer.
    if (!RequireFile(witness, report))
    {
        return Fail(report, "windows_runtime_witness_source");
    }
    observation.ObservedInputs.push_back(witness);
    const auto witnessObject = work / "runtime-witness.obj";
    const auto witnessBinary = work / "runtime-witness.exe";
    if (!CheckObservationOutput(witnessObject, report) || !CheckObservationOutput(witnessBinary, report))
    {
        return Fail(report, "windows_witness_output_alias");
    }
    auto compile = witnessFlags;
    compile.insert(compile.end(), {"-c", witness.generic_string(), "-o", witnessObject.generic_string()});
    if (!Execute(request.Compiler, witnessDirectory, std::move(compile), report))
    {
        return Fail(report, "windows_runtime_witness_compile");
    }
    const auto linked = Execute(request.Compiler, witnessDirectory,
                                {
                                    "-nostartfiles",
                                    "-nostdlib",
                                    "-fuse-ld=lld",
                                    witnessObject.generic_string(),
                                    "-o",
                                    witnessBinary.generic_string(),
                                    "-Xlinker",
                                    "/subsystem:console",
                                    "-Xlinker",
                                    "/verbose",
                                    "-lkernel32",
                                },
                                report);
    if (!linked)
    {
        return std::unexpected(linked.error());
    }
    const auto trace = linked->Stdout + "\n" + linked->Stderr;
    Json libraries = Json::array();
    for (std::size_t start = 0; start < trace.size();)
    {
        const auto end = trace.find('\n', start);
        const auto line = Trim(std::string_view(trace).substr(start, end == std::string::npos ? end : end - start));
        constexpr std::string_view kMarker = "lld-link: Reading ";
        if (line.starts_with(kMarker) && line.ends_with(".lib"))
        {
            const auto library = Path(line.substr(kMarker.size()));
            if (library.is_absolute() && std::filesystem::is_regular_file(library))
            {
                const auto normalized = Normalize(library);
                const auto toolsetLib = Normalize(vcRoot / "tools/msvc" / *toolsetVersion / "lib/x64") + "/";
                const auto sdkLib = Normalize(sdkRoot / "lib" / *sdkVersion) + "/";
                if (!normalized.starts_with(toolsetLib) && !normalized.starts_with(sdkLib))
                {
                    return Fail(report, "windows_runtime_outside_sdk");
                }
                if (std::ranges::none_of(libraries,
                                         [&](const auto& entry) { return entry.is_string() && entry == normalized; }))
                {
                    libraries.push_back(normalized);
                    observation.ObservedInputs.push_back(library);
                }
            }
        }
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    if (libraries.empty())
    {
        return Fail(report, "windows_missing_runtime_link_trace");
    }
    evidence.update(Json{{"runtime_libraries", std::move(libraries)}});
    observation.Facts = std::move(evidence);
    return (*ownership)->Finish(std::move(observation));
#endif
}

Result<CollectedProfile> CollectWindowsPlatformProfile(const CollectionRequest& request,
                                                       std::span<const CompileUnit> units,
                                                       const CompileObservation& observation, FailureReport& report)
{
    if (request.TargetPlatform != Platform::kWindows || units.empty() || !observation.Facts.is_object())
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_collection_inputs"));
    }
    // The pre-compile observation cannot inspect objects that do not exist yet.
    // Validate real COFF directives here, after successful compilation.
    auto evidence = observation.Facts;
    if (!evidence.contains("units") || !evidence.at("units").is_array() || evidence.at("units").size() != units.size())
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_object_manifest_changed"));
    }
    const auto readobj = request.Compiler.parent_path() / "llvm-readobj.exe";
    for (std::size_t index = 0; index < units.size(); ++index)
    {
        const auto& unit = units.subspan(index).front();
        if (!std::filesystem::is_regular_file(unit.Object) ||
            evidence.at("units").at(index).at("object") != unit.Object.generic_string())
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_missing_or_mismatched_object",
                                              unit.Object.generic_string()));
        }
        const auto directives =
            Execute(readobj, unit.Directory, {"--coff-directives", unit.Object.generic_string()}, report);
        if (!directives)
        {
            return std::unexpected(directives.error());
        }
        evidence.at("units").at(index).update(Json{{"directives", directives->Stdout}});
    }
    auto profile = BuildWindowsProfile(evidence, report);
    if (!profile)
    {
        return std::unexpected(profile.error());
    }
    const auto raw = RequireFile(request.SchemaFile, report);
    if (!raw)
    {
        return std::unexpected(raw.error());
    }
    const auto schema = ParseUnique(*raw);
    if (!schema || !Validate(*schema, *profile))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_frozen_schema"));
    }
    return CollectedProfile{
        .Profile = std::move(*profile),
        .Evidence = std::move(evidence),
        .ObservedInputs = observation.ObservedInputs,
    };
}
} // namespace pg::abi
