#include "AppleCollector.h"

#include "CollectionRequest.h"
#include "CollectionState.h"
#include "CompileCommands.h"
#include "HeaderTrace.h" // IWYU pragma: keep - Clang -H trace parser.
#include "JsonSchema.h"
#include "Observation.h"
#include "Process.h"
#include "ToolIo.h"
#include "ToolReport.h"

#include <Result.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
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
                                            std::vector<std::string> arguments, FailureReport& report)
{
    return ExecuteObservation(program, working, std::move(arguments), "apple_probe_exit", report);
}

// The linker assigns LC_UUID to each Mach-O witness independently.
// It does not describe Target ABI, and retaining it in CompileDigest would
// reject an unchanged object after two otherwise identical witness links.
[[nodiscard, maybe_unused]] Result<std::string> StableMachOBuildOutput(std::string text, FailureReport& report)
{
    const auto command = text.find("cmd LC_UUID");
    if (command == std::string::npos)
    {
        return text;
    }
    constexpr std::string_view kUuidLine = "\n    uuid ";
    const auto uuidLine = text.find(kUuidLine, command);
    if (uuidLine == std::string::npos || uuidLine - command > 80)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_macho_uuid_shape"));
    }
    const auto begin = uuidLine + kUuidLine.size();
    const auto end = text.find('\n', begin);
    if (end == std::string::npos || end - begin != 36)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_macho_uuid_shape"));
    }
    text.replace(begin, end - begin, "<link-generated>");
    return text;
}

[[nodiscard]] Result<std::string> Required(const std::filesystem::path& path, FailureReport& report)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error)
    {
        return std::unexpected(report.Add(ToolError::kReadInput, "apple_missing_input", path.generic_string(), error));
    }
    return ReadFile(path, "apple_read_provenance", report);
}

[[nodiscard]] std::string_view Macro(std::string_view macros, std::string_view name)
{
    const auto prefix = "#define " + std::string(name) + " ";
    const auto position = macros.find(prefix);
    if (position == std::string_view::npos || (position > 0 && macros.at(position - 1) != '\n'))
    {
        return {};
    }
    const auto start = position + prefix.size();
    const auto end = macros.find('\n', start);
    return macros.substr(start, end == std::string_view::npos ? end : end - start);
}

[[nodiscard]] std::string Triple(std::string_view output)
{
    constexpr std::string_view kMarker = R"("-triple" ")";
    const auto position = output.find(kMarker);
    if (position == std::string_view::npos)
    {
        return {};
    }
    const auto begin = position + kMarker.size();
    const auto end = output.find('"', begin);
    return end == std::string_view::npos ? std::string{} : std::string(output.substr(begin, end - begin));
}

[[nodiscard, maybe_unused]] Result<std::filesystem::path> SdkFromArgs(std::span<const std::string> flags,
                                                                      FailureReport& report)
{
    std::filesystem::path sdk;
    for (std::size_t index = 0; index < flags.size(); ++index)
    {
        const auto& arg = flags.subspan(index).front();
        if (arg == "-isysroot" && index + 1 < flags.size())
        {
            if (!sdk.empty())
            {
                return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_duplicate_sysroot"));
            }
            sdk = flags.subspan(++index).front();
        }
        else if (arg.starts_with("--sysroot") || arg.starts_with("-isysroot") || arg.starts_with("-target") ||
                 arg.starts_with("--target") || arg.starts_with("-D_LIBCPP_") || arg.starts_with("-U_LIBCPP_") ||
                 arg.starts_with("-fpack") || arg.starts_with("-fshort-") || arg.starts_with("-mcpu") ||
                 arg.starts_with("-march"))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_abi_options", arg));
        }
    }
    if (sdk.empty() || !sdk.is_absolute())
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_missing_sdk"));
    }
    return sdk.lexically_normal();
}

[[nodiscard]] Result<std::string> SdkVersion(const std::filesystem::path& sdk, Platform platform, FailureReport& report)
{
    const auto jsonText = Required(sdk / "SDKSettings.json", report);
    if (!jsonText || !Required(sdk / "SDKSettings.plist", report))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_sdk_settings"));
    }
    const auto data = ParseUnique(*jsonText);
    if (!data || !data->is_object() || !data->contains("Version") || !data->contains("CanonicalName") ||
        !data->at("Version").is_string() || !data->at("CanonicalName").is_string())
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_sdk_manifest"));
    }
    const bool ios = platform == Platform::kIos;
    auto version = data->at("Version").get<std::string>();
    const auto canonical = data->at("CanonicalName").get<std::string>();
    if (version != (ios ? "18.2" : "15.2") || canonical != (ios ? "iphoneos18.2" : "macosx15.2"))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_unsupported_sdk_version"));
    }
    // An independent native parser checks that the binary plist agrees with JSON.
    const auto plist = Execute("/usr/bin/plutil", sdk,
                               {"-extract", "Version", "raw", (sdk / "SDKSettings.plist").generic_string()}, report);
    if (!plist || (plist->Stdout != version && plist->Stdout != version + "\n"))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_sdk_plist_mismatch"));
    }
    const std::string_view flavor = ios ? "arm64-ios" : "x86_64-macos";
    for (const auto& [filename, dylib] : {
             std::pair<std::string_view, std::string_view>{"libc++.tbd", "/usr/lib/libc++.1.dylib"},
             {"libc++abi.tbd", "/usr/lib/libc++abi.dylib"},
             {"libSystem.tbd", "/usr/lib/libSystem.B.dylib"},
         })
    {
        const auto stub = Required(sdk / "usr/lib" / filename, report);
        if (!stub || !stub->contains(flavor) || !stub->contains(dylib))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_stub_provider", filename));
        }
    }
    return version;
}

[[nodiscard]] Result<Json> Profile(const Json& unit, Platform platform, const Json& observation, FailureReport& report)
{
    const auto text = unit.at("macros").get<std::string>();
    if (!unit.contains("libcxx_baseline") || !unit.at("libcxx_baseline").is_string() ||
        !CheckLibcxxConfiguration(text, unit.at("libcxx_baseline").get_ref<const std::string&>(), report))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_libcxx_configuration"));
    }
    const auto sdkVersion = observation.at("sdk_version").get<std::string>();
    const std::string_view expected =
        platform == Platform::kIos ? "arm64-apple-ios18.0.0" : "x86_64-apple-macosx14.0.0";
    const auto triple = Triple(unit.at("driver").get<std::string>());
    const auto ios = platform == Platform::kIos;
    const auto macosTarget = Macro(text, "TARGET_OS_IPHONE");
    const auto minor = Macro(text, "__clang_minor__");
    const auto patch = Macro(text, "__clang_patchlevel__");
    if (triple != expected || Macro(text, "_LIBCPP_VERSION") != "230102" || Macro(text, "_LIBCPP_ABI_VERSION") != "1" ||
        Macro(text, "_LIBCPP_ABI_NAMESPACE") != "__1" ||
        Macro(text, "_LIBCPP_HARDENING_MODE") != "_LIBCPP_HARDENING_MODE_DEFAULT" ||
        Macro(text, "_LIBCPP_HARDENING_MODE_DEFAULT") != "2" || !Macro(text, "_LIBCPP_ABI_UNSTABLE").empty() ||
        !Macro(text, "_LIBCPP_ABI_ALTERNATE_STRING_LAYOUT").empty() || Macro(text, "__cplusplus") != "202302L" ||
        Macro(text, "__SIZEOF_POINTER__") != "8" || Macro(text, "__SIZEOF_WCHAR_T__") != "4" ||
        Macro(text, "__BYTE_ORDER__") != "__ORDER_LITTLE_ENDIAN__" || !Macro(text, "__CHAR_UNSIGNED__").empty() ||
        Macro(text, "__clang_major__") != "23" || minor != "1" || patch != "2" ||
        !Macro(text, "__EXCEPTIONS").empty() || !Macro(text, "__cpp_exceptions").empty() ||
        macosTarget != (ios ? "1" : "0") ||
        (!ios && (!Macro(text, "__AVX__").empty() || !Macro(text, "__SSE4_2__").empty())))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_abi_macros", triple));
    }
    const auto driver = unit.at("driver").get<std::string>();
    if (!driver.contains("\"-target-sdk-version=" + sdkVersion + "\"") ||
        !observation.at("compiler_banner").get<std::string>().contains("clang version 23.1.2"))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_frontend_sdk_version"));
    }
    const std::string_view rtti = Macro(text, "__GXX_RTTI").empty() ? "off" : "on";
    return Json{
        {"schema", 1},
        {"policy", "strict-v1"},
        {
            "target",
            {
                {"triple", triple},
                {"object", "macho"},
                {"pointer_bits", "64"},
                {"endian", "little"},
                {"cpu_abi", ios ? "aapcs64" : "baseline-x86_64"},
            },
        },
        {
            "compiler",
            {
                {"id", "clang"},
                {"distribution", "macports"},
                {"version", "23.1.2"},
                {"cxx_abi", "itanium"},
            },
        },
        {
            "language",
            {
                {"standard", "c++23"},
                {"exceptions", "off"},
                {"rtti", rtti},
                {
                    "abi_options",
                    {
                        "bitfield_layout=itanium",
                        ios ? "calling_convention=aapcs64" : "calling_convention=sysv",
                        "char_signedness=signed",
                        "enum_width=native",
                        "float_abi=default",
                        "struct_pack=default",
                        ios ? "vector_abi=aapcs64" : "vector_abi=default",
                        "wchar_width=32",
                    },
                },
            },
        },
        {
            "stdlib",
            {
                {"id", "libc++"},
                {"version", "23.1.2"},
                {"abi_version", "1"},
                {"abi_namespace", "__1"},
                {"iterator_debug_level", "na"},
            },
        },
        {
            "runtime",
            {
                {"crt", "apple-libsystem"},
                {"cxx_runtime", "apple-libcxx-shared"},
                {"cxxabi_runtime", "apple-libcxxabi"},
                {"sdk_abi_id", (ios ? "ios-sdk-" : "macos-sdk-") + sdkVersion},
            },
        },
    };
}
} // namespace

Result<CompileObservation> InspectAppleCompileInputs(const CollectionRequest& request,
                                                     std::span<const CompileUnit> units, FailureReport& report)
{
#ifndef __APPLE__
    (void)request;
    (void)units;
    return std::unexpected(report.Add(ToolError::kInvalidRequest, "apple_requires_macos_host"));
#else
    if (request.HostPlatform != Platform::kMacos ||
        (request.TargetPlatform != Platform::kIos && request.TargetPlatform != Platform::kMacos) || units.empty())
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_unsupported_host_target"));
    }
    // The configured compiler selects the installation. Verify its matching
    // resources and physical header origins, independent of a package prefix.
    const auto compilerRoot = request.Compiler.parent_path().parent_path();
    const auto stl = compilerRoot / "include/c++/v1";
    const auto site = stl / "__config_site";
    if (!Required(stl / "version", report) || !Required(site, report))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_unverified_libcxx"));
    }
    const auto banner = Execute(request.Compiler, request.BuildRoot, {"--version"}, report);
    if (!banner || !banner->Stdout.contains("clang version 23.1.2"))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_compiler_version"));
    }
    const auto resource = Execute(request.Compiler, request.BuildRoot, {"--print-resource-dir"}, report);
    if (!resource)
    {
        return std::unexpected(resource.error());
    }
    const auto resourceDirectory =
        std::filesystem::path(resource->Stdout.substr(0, resource->Stdout.find_first_of("\r\n")));
    std::error_code resourceError;
    if (!resourceDirectory.is_absolute() ||
        !std::filesystem::equivalent(resourceDirectory, compilerRoot / "lib/clang/23", resourceError) ||
        resourceError || !Required(resourceDirectory / "include/stddef.h", report))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_compiler_resources"));
    }
    const auto probe = request.SourceRoot / "Tools/AbiProfile/Probe/AppleLibcxx.cpp";
    const auto witness = request.SourceRoot / "Tools/AbiProfile/Probe/PosixRuntimeWitness.cpp";
    if (!Required(probe, report) || !Required(witness, report))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_probe_files"));
    }
    const std::array knownInputs{probe, witness};
    const auto ownership = ObservationWork::Acquire(request, units, knownInputs, "apple_probe_exit", report);
    if (!ownership)
    {
        return std::unexpected(ownership.error());
    }
    const auto work = (*ownership)->Directory();
    std::filesystem::path sdk;
    std::string version;
    std::vector<std::string> witnessFlags;
    std::filesystem::path witnessDirectory;
    CompileObservation result;
    result.ObservedInputs = {probe, witness, site, stl / "version"};
    Json facts = {
        {"compiler_banner", banner->Stdout},
        {"units", Json::array()},
    };
    for (std::size_t index = 0; index < units.size(); ++index)
    {
        const auto& unit = units.subspan(index).front();
        if (const auto checked = CheckLibcxxDriverArguments(unit.AbiArguments, report); !checked)
        {
            return std::unexpected(checked.error());
        }
        const auto foundSdk = SdkFromArgs(unit.AbiArguments, report);
        if (!foundSdk)
        {
            return std::unexpected(foundSdk.error());
        }
        const auto foundVersion = SdkVersion(*foundSdk, request.TargetPlatform, report);
        if (!foundVersion || (!sdk.empty() && sdk != *foundSdk) || (!version.empty() && version != *foundVersion))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_sdk_disagreement"));
        }
        sdk = *foundSdk;
        version = *foundVersion;
        std::error_code compilerError;
        if (!std::filesystem::equivalent(unit.Compiler, request.Compiler, compilerError) || compilerError)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_mixed_compiler"));
        }
        const auto observed = ObserveTranslationUnit(request, unit, probe, work, index, "apple_probe_exit", report);
        if (!observed)
        {
            return std::unexpected(observed.error());
        }
        const auto* macros = &observed->Macros;
        const auto* driver = &observed->Driver;
        const auto headerTrace = macros->Stderr;
        const auto headers = ReadClangHeaderTrace(headerTrace, unit.Directory, report);
        const std::array<std::string_view, 4> stlKeys{"version", "__config", "__config_site", "__configuration/abi.h"};
        const std::array<std::string_view, 1> sdkKeys{"TargetConditionals.h"};
        if (!headers || !CheckClangHeaderOrigins(*headers, stl, stlKeys, report) ||
            !CheckClangHeaderOrigins(*headers, sdk / "usr/include", sdkKeys, report) ||
            !driver->Stderr.contains(sdk.generic_string()))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_header_origin"));
        }
        std::vector<std::string> baselineFlags{
            "-std=c++23",
            "-fno-exceptions",
            "-target",
            request.TargetPlatform == Platform::kIos ? "arm64-apple-ios18.0.0" : "x86_64-apple-macosx14.0.0",
            "-isysroot",
            sdk.generic_string(),
            "-nostdinc++",
            "-isystem",
            stl.generic_string(),
        };
        if (Macro(macros->Stdout, "__GXX_RTTI").empty())
        {
            baselineFlags.emplace_back("-fno-rtti");
        }
        baselineFlags.insert(baselineFlags.end(), {"-E", "-dM", "-H", probe.generic_string()});
        const auto cleanConfig = Execute(request.Compiler, unit.Directory, std::move(baselineFlags), report);
        if (!cleanConfig || !CheckLibcxxConfiguration(macros->Stdout, cleanConfig->Stdout, report))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_libcxx_configuration"));
        }
        std::error_code originError;
        const auto selectedHeaders = std::filesystem::canonical(stl, originError);
        const auto selectedSite = std::filesystem::canonical(site, originError);
        if (originError ||
            !CheckLibcxxSourceConfiguration(unit.Source, *headers, selectedHeaders, selectedSite, report))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_libcxx_definitions"));
        }
        for (const auto& header : *headers)
        {
            result.Headers.push_back(header.Spelling);
        }
        facts.at("units").push_back({
            {"source", unit.Source.generic_string()},
            {"object", unit.Object.generic_string()},
            {"macros", macros->Stdout},
            {"libcxx_baseline", cleanConfig->Stdout},
            {"driver", driver->Stderr},
            {"arguments", unit.AbiArguments},
            {"headers", headerTrace},
        });
        if (index == 0)
        {
            witnessFlags = unit.AbiArguments;
            witnessDirectory = unit.Directory;
        }
    }
    // Apple SDK metadata and stubs are inspected at their actual selected paths.
    result.ObservedInputs.insert(result.ObservedInputs.end(), {
                                                                  sdk / "SDKSettings.json",
                                                                  sdk / "SDKSettings.plist",
                                                                  sdk / "usr/lib/libc++.tbd",
                                                                  sdk / "usr/lib/libc++abi.tbd",
                                                                  sdk / "usr/lib/libSystem.tbd",
                                                                  sdk / "usr/include/TargetConditionals.h",
                                                              });
    facts.update(Json{{"sdk", sdk.generic_string()}, {"sdk_version", version}});
    const auto binary = work / "runtime-witness";
    if (const auto safe = CheckObservationOutput(binary, report); !safe)
    {
        return std::unexpected(safe.error());
    }
    witnessFlags.insert(witnessFlags.end(), {
                                                witness.generic_string(),
                                                "-stdlib=libc++",
                                                "-o",
                                                binary.generic_string(),
                                            });
    if (!Execute(request.Compiler, witnessDirectory, std::move(witnessFlags), report))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_link_witness"));
    }
    const auto readobj = request.Compiler.parent_path() / "llvm-readobj";
    const auto needed = Execute(readobj, witnessDirectory, {"--needed-libs", binary.generic_string()}, report);
    const auto header = Execute(readobj, witnessDirectory, {"--file-headers", binary.generic_string()}, report);
    const auto build = Execute("/usr/bin/otool", witnessDirectory, {"-l", binary.generic_string()}, report);
    const bool ios = request.TargetPlatform == Platform::kIos;
    if (!needed || !header || !build || !needed->Stdout.contains("/usr/lib/libc++.1.dylib") ||
        !needed->Stdout.contains("/usr/lib/libSystem.B.dylib") ||
        !header->Stdout.contains(ios ? "Mach-O arm64" : "Mach-O 64-bit x86-64") ||
        !build->Stdout.contains(ios ? "platform 2" : "platform 1") ||
        !build->Stdout.contains(ios ? "sdk 18.2" : "sdk 15.2"))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_link_runtime_or_platform"));
    }
    const auto stableBuild = StableMachOBuildOutput(build->Stdout, report);
    if (!stableBuild)
    {
        return std::unexpected(stableBuild.error());
    }
    facts.update(Json{
        {"runtime_needed", needed->Stdout},
        {"runtime_macho_header", header->Stdout},
        {"runtime_macho_build", *stableBuild},
    });
    result.Facts = std::move(facts);
    return (*ownership)->Finish(std::move(result));
#endif
}

Result<CollectedProfile> CollectApplePlatformProfile(const CollectionRequest& request,
                                                     std::span<const CompileUnit> units,
                                                     const CompileObservation& observation, FailureReport& report)
{
    if (request.HostPlatform != Platform::kMacos || units.empty() || !observation.Facts.is_object() ||
        !observation.Facts.contains("units") || observation.Facts.at("units").size() != units.size())
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_collection_inputs"));
    }
    const auto sdk = std::filesystem::path(observation.Facts.at("sdk").get<std::string>());
    const auto current = SdkVersion(sdk, request.TargetPlatform, report);
    if (!current || observation.Facts.at("sdk_version") != *current)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_sdk_evidence_changed"));
    }
    const auto& facts = observation.Facts;
    if (!facts.contains("runtime_needed") || !facts.at("runtime_needed").is_string() ||
        !facts.at("runtime_needed").get<std::string>().contains("/usr/lib/libc++.1.dylib") ||
        !facts.at("runtime_needed").get<std::string>().contains("/usr/lib/libSystem.B.dylib"))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_runtime_evidence_changed"));
    }
    const auto* machoBuild = Find(facts, "runtime_macho_build");
    const auto* machoHeader = Find(facts, "runtime_macho_header");
    const bool ios = request.TargetPlatform == Platform::kIos;
    if (machoBuild == nullptr || !machoBuild->is_string() ||
        !machoBuild->get<std::string>().contains(ios ? "platform 2" : "platform 1") ||
        !machoBuild->get<std::string>().contains(ios ? "sdk 18.2" : "sdk 15.2") || machoHeader == nullptr ||
        !machoHeader->is_string() ||
        !machoHeader->get<std::string>().contains(ios ? "Mach-O arm64" : "Mach-O 64-bit x86-64"))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_macho_runtime_evidence_changed"));
    }
    const auto readobj = request.Compiler.parent_path() / "llvm-readobj";
    std::optional<Json> baseline;
    for (std::size_t index = 0; index < units.size(); ++index)
    {
        const auto& unit = units.subspan(index).front();
        if (const auto checked = CheckLibcxxDriverArguments(unit.AbiArguments, report); !checked)
        {
            return std::unexpected(checked.error());
        }
        const auto& unitEvidence = facts.at("units").at(index);
        if (!std::filesystem::is_regular_file(unit.Object) || unitEvidence.at("object") != unit.Object.generic_string())
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_object_identity"));
        }
        const auto header = Execute(readobj, unit.Directory, {"--file-headers", unit.Object.generic_string()}, report);
        const bool ios = request.TargetPlatform == Platform::kIos;
        if (!header || !header->Stdout.contains(ios ? "Mach-O arm64" : "Mach-O 64-bit x86-64"))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_object_format"));
        }
        const auto mapped = Profile(unitEvidence, request.TargetPlatform, facts, report);
        if (!mapped)
        {
            return std::unexpected(mapped.error());
        }
        if (baseline && *baseline != *mapped)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_tu_disagreement"));
        }
        baseline = *mapped;
    }
    const auto schemaText = Required(request.SchemaFile, report);
    if (!baseline || !schemaText)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_schema_missing"));
    }
    const auto schema = ParseUnique(*schemaText);
    if (!schema || !Validate(*schema, *baseline))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "apple_frozen_schema"));
    }
    return CollectedProfile{
        .Profile = std::move(*baseline),
        .Evidence = observation.Facts,
        .ObservedInputs = observation.ObservedInputs,
    };
}
} // namespace pg::abi
