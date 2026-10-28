#include "AndroidCollector.h"

#include "CollectionRequest.h"
#include "CollectionState.h"
#include "CompileCommands.h"
#include "HeaderTrace.h"
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
[[nodiscard]] Result<ProcessResult> Execute(const std::filesystem::path& program,
                                            const std::filesystem::path& directory, std::vector<std::string> arguments,
                                            FailureReport& report)
{
    return ExecuteObservation(program, directory, std::move(arguments), "android_probe_exit", report);
}

[[nodiscard]] Result<std::string> Required(const std::filesystem::path& path, FailureReport& report)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error)
    {
        return std::unexpected(
            report.Add(ToolError::kReadInput, "android_missing_input", path.generic_string(), error));
    }
    return ReadFile(path, "android_read_provenance", report);
}

[[nodiscard]] std::string_view Macro(std::string_view definitions, std::string_view name)
{
    const auto marker = "#define " + std::string(name) + " ";
    const auto pos = definitions.find(marker);
    if (pos == std::string_view::npos || (pos != 0 && definitions.at(pos - 1) != '\n'))
    {
        return {};
    }
    const auto offset = pos + marker.size();
    const auto end = definitions.find('\n', offset);
    auto value = definitions.substr(offset, end == std::string_view::npos ? end : end - offset);
    // Strip only the CR in a CRLF terminator; other value bytes remain ABI-significant.
    if (end != std::string_view::npos && value.ends_with('\r'))
    {
        value.remove_suffix(1);
    }
    return value;
}

[[nodiscard]] std::string DriverTriple(std::string_view driver)
{
    constexpr std::string_view kMarker = R"("-triple" ")";
    const auto pos = driver.find(kMarker);
    if (pos == std::string_view::npos)
    {
        return {};
    }
    const auto begin = pos + kMarker.size();
    const auto end = driver.find('"', begin);
    return end == std::string_view::npos ? std::string{} : std::string(driver.substr(begin, end - begin));
}

[[nodiscard]] std::string_view Property(std::string_view source, std::string_view key)
{
    const auto marker = std::string(key) + " = ";
    const auto pos = source.find(marker);
    if (pos == std::string_view::npos || (pos != 0 && source.at(pos - 1) != '\n'))
    {
        return {};
    }
    const auto begin = pos + marker.size();
    const auto end = source.find_first_of("\r\n", begin);
    return source.substr(begin, end == std::string_view::npos ? end : end - begin);
}

struct Ndk
{
    std::filesystem::path Installation;
    std::filesystem::path Toolchain;
    std::filesystem::path Sysroot;
    std::filesystem::path Include;
    std::filesystem::path Runtime;
    std::filesystem::path Properties;
};

[[nodiscard]] Result<Ndk> IdentifyNdk(const CollectionRequest& request, FailureReport& report)
{
    if (request.HostPlatform != Platform::kWindows || request.TargetPlatform != Platform::kAndroid)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_requires_windows_host"));
    }
    Ndk ndk;
    ndk.Toolchain = request.Compiler.parent_path().parent_path();
    ndk.Installation = ndk.Toolchain.parent_path().parent_path().parent_path().parent_path();
    ndk.Sysroot = ndk.Toolchain / "sysroot";
    ndk.Include = ndk.Sysroot / "usr/include";
    ndk.Runtime = ndk.Sysroot / "usr/lib/aarch64-linux-android";
    ndk.Properties = ndk.Installation / "source.properties";
    const auto properties = Required(ndk.Properties, report);
    if (!properties || Property(*properties, "Pkg.ReleaseName") != "r30" ||
        Property(*properties, "Pkg.Revision") != "30.0.16248370" ||
        Property(*properties, "Pkg.BaseRevision") != "30.0.16248370")
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_ndk_release_revision"));
    }
    const auto expected = ndk.Toolchain / "bin/clang++.exe";
    std::error_code error;
    if (!std::filesystem::equivalent(expected, request.Compiler, error) || error)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_unverified_ndk_clang"));
    }
    for (const auto& file : {
             ndk.Include / "c++/v1/version",
             ndk.Include / "c++/v1/__config_site",
             ndk.Include / "android/ndk-version.h",
             ndk.Include / "android/api-level.h",
             ndk.Include / "sys/cdefs.h",
             ndk.Runtime / "libc++_shared.so",
             ndk.Runtime / "29/libc.so",
             ndk.Runtime / "29/libc++.so",
         })
    {
        if (!Required(file, report))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_ndk_runtime_sources"));
        }
    }
    return ndk;
}

[[nodiscard]] Status CheckArguments(std::span<const std::string> arguments, const Ndk& ndk, FailureReport& report)
{
    bool seenTarget = false;
    bool seenSysroot = false;
    for (const auto& arg : arguments)
    {
        if (arg.starts_with("--target="))
        {
            if (seenTarget || arg != "--target=aarch64-none-linux-android29")
            {
                return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_target_option", arg));
            }
            seenTarget = true;
        }
        else if (arg.starts_with("--sysroot="))
        {
            const auto value = std::filesystem::path(arg.substr(std::string("--sysroot=").size()));
            std::error_code error;
            if (seenSysroot || !std::filesystem::equivalent(ndk.Sysroot, value, error) || error)
            {
                return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_sysroot_option", arg));
            }
            seenSysroot = true;
        }
        else if (arg.starts_with("-target") || arg.starts_with("-isysroot") || arg.starts_with("-march") ||
                 arg.starts_with("-mcpu") || arg.starts_with("-mabi") || arg.starts_with("-fpack") ||
                 arg.starts_with("-fshort-") || arg.starts_with("-D_LIBCPP_") || arg.starts_with("-U_LIBCPP_"))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_unknown_abi_option", arg));
        }
    }
    if (!seenTarget || !seenSysroot)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_missing_target_or_sysroot"));
    }
    return {};
}

[[nodiscard]] Result<Json> MapProfile(const Json& unit, const Json& observations, FailureReport& report)
{
    const auto macros = unit.at("macros").get<std::string>();
    if (!unit.contains("libcxx_baseline") || !unit.at("libcxx_baseline").is_string() ||
        !CheckLibcxxConfiguration(macros, unit.at("libcxx_baseline").get_ref<const std::string&>(), report))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_libcxx_configuration"));
    }
    const auto triple = DriverTriple(unit.at("driver").get<std::string>());
    if (triple != "aarch64-none-linux-android29" || Macro(macros, "__ANDROID__") != "1" ||
        Macro(macros, "__ANDROID_NDK__") != "1" || Macro(macros, "__ANDROID_MIN_SDK_VERSION__") != "29" ||
        Macro(macros, "__ANDROID_API__") != "__ANDROID_MIN_SDK_VERSION__" || Macro(macros, "__NDK_MAJOR__") != "30" ||
        Macro(macros, "__NDK_MINOR__") != "0" || Macro(macros, "__NDK_BUILD__") != "16248370" ||
        Macro(macros, "__NDK_BETA__") != "0" || Macro(macros, "__NDK_CANARY__") != "0" ||
        Macro(macros, "_LIBCPP_VERSION") != "210000" || Macro(macros, "_LIBCPP_ABI_VERSION") != "1" ||
        Macro(macros, "_LIBCPP_ABI_NAMESPACE") != "__ndk1" ||
        Macro(macros, "_LIBCPP_HARDENING_MODE") != "_LIBCPP_HARDENING_MODE_DEFAULT" ||
        Macro(macros, "_LIBCPP_HARDENING_MODE_DEFAULT") != "2" || !Macro(macros, "_LIBCPP_ABI_UNSTABLE").empty() ||
        !Macro(macros, "_LIBCPP_ABI_ALTERNATE_STRING_LAYOUT").empty() || Macro(macros, "__clang_major__") != "21" ||
        Macro(macros, "__clang_minor__") != "0" || Macro(macros, "__clang_patchlevel__") != "0" ||
        Macro(macros, "__cplusplus") != "202302L" || Macro(macros, "__SIZEOF_POINTER__") != "8" ||
        Macro(macros, "__SIZEOF_WCHAR_T__") != "4" || Macro(macros, "__BYTE_ORDER__") != "__ORDER_LITTLE_ENDIAN__" ||
        Macro(macros, "__CHAR_UNSIGNED__") != "1" || !Macro(macros, "__EXCEPTIONS").empty() ||
        !Macro(macros, "__cpp_exceptions").empty() || !Macro(macros, "__ARM_FEATURE_SVE").empty() ||
        !observations.at("compiler_banner").get<std::string>().contains("clang version 21.0.0"))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_abi_macros_or_triple",
                                          unit.at("source").get<std::string>()));
    }
    const bool rtti = !Macro(macros, "__GXX_RTTI").empty();
    return Json{
        {"schema", 1},
        {"policy", "strict-v1"},
        {
            "target",
            {
                {"triple", triple},
                {"object", "elf"},
                {"pointer_bits", "64"},
                {"endian", "little"},
                {"cpu_abi", "aapcs64"},
            },
        },
        {
            "compiler",
            {
                {"id", "clang"},
                {"distribution", "android-ndk"},
                {"version", "21.0.0"},
                {"cxx_abi", "itanium"},
            },
        },
        {
            "language",
            {
                {"standard", "c++23"},
                {"exceptions", "off"},
                {"rtti", rtti ? "on" : "off"},
                {
                    "abi_options",
                    {
                        "bitfield_layout=itanium",
                        "calling_convention=aapcs64",
                        "char_signedness=unsigned",
                        "enum_width=native",
                        "float_abi=default",
                        "struct_pack=default",
                        "vector_abi=aapcs64",
                        "wchar_width=32",
                    },
                },
            },
        },
        {
            "stdlib",
            {
                {"id", "libc++"},
                {"version", "21.0.0"},
                {"abi_version", "1"},
                {"abi_namespace", "__ndk1"},
                {"iterator_debug_level", "na"},
            },
        },
        {
            "runtime",
            {
                {"crt", "bionic"},
                {"cxx_runtime", "ndk-libcxx-shared"},
                {"cxxabi_runtime", "ndk-libcxxabi"},
                {"sdk_abi_id", "android-ndk-r30-api-29"},
            },
        },
    };
}
} // namespace

Result<CompileObservation> InspectAndroidCompileInputs(const CollectionRequest& request,
                                                       std::span<const CompileUnit> units, FailureReport& report)
{
#ifndef _WIN32
    (void)request;
    (void)units;
    return std::unexpected(report.Add(ToolError::kInvalidRequest, "android_windows_host_only"));
#else
    if (units.empty())
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_empty_units"));
    }
    const auto identified = IdentifyNdk(request, report);
    if (!identified)
    {
        return std::unexpected(identified.error());
    }
    const auto& ndk = *identified;
    const auto banner = Execute(request.Compiler, request.BuildRoot, {"--version"}, report);
    if (!banner || !banner->Stdout.contains("clang version 21.0.0"))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_unverified_clang_version"));
    }
    const auto probe = request.SourceRoot / "BuildTools/AbiProfile/Probe/AndroidLibcxx.cpp";
    const auto witness = request.SourceRoot / "BuildTools/AbiProfile/Probe/PosixRuntimeWitness.cpp";
    if (!Required(probe, report) || !Required(witness, report))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_missing_probes"));
    }
    const std::array knownInputs{probe, witness};
    const auto ownership = ObservationWork::Acquire(request, units, knownInputs, "android_probe_exit", report);
    if (!ownership)
    {
        return std::unexpected(ownership.error());
    }
    const auto work = (*ownership)->Directory();
    CompileObservation observation;
    observation.ObservedInputs = {
        ndk.Properties,
        ndk.Include / "c++/v1/version",
        ndk.Include / "c++/v1/__config_site",
        ndk.Include / "android/ndk-version.h",
        ndk.Include / "android/api-level.h",
        ndk.Include / "sys/cdefs.h",
        ndk.Runtime / "libc++_shared.so",
        ndk.Runtime / "29/libc.so",
        ndk.Runtime / "29/libc++.so",
        probe,
        witness,
    };
    Json facts = {
        {"compiler_banner", banner->Stdout},
        {"sdk_id", "android-ndk-r30-api-29"},
        {"ndk_release", "r30"},
        {"ndk_revision", "30.0.16248370"},
        {"ndk_properties", ndk.Properties.generic_string()},
        {"runtime_shared", (ndk.Runtime / "libc++_shared.so").generic_string()},
        {"runtime_bionic", (ndk.Runtime / "29/libc.so").generic_string()},
        {"units", Json::array()},
    };
    for (std::size_t index = 0; index < units.size(); ++index)
    {
        const auto& unit = units.subspan(index).front();
        std::error_code compilerError;
        if (!std::filesystem::equivalent(unit.Compiler, request.Compiler, compilerError) || compilerError)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_mixed_ndk_compilers"));
        }
        if (const auto checked = CheckLibcxxDriverArguments(unit.AbiArguments, report); !checked)
        {
            return std::unexpected(checked.error());
        }
        if (const auto valid = CheckArguments(unit.AbiArguments, ndk, report); !valid)
        {
            return std::unexpected(valid.error());
        }
        const auto observed = ObserveTranslationUnit(request, unit, probe, work, index, "android_probe_exit", report);
        if (!observed)
        {
            return std::unexpected(observed.error());
        }
        const auto* macros = &observed->Macros;
        const auto* driver = &observed->Driver;
        const auto headersFromClang = ReadClangHeaderTrace(macros->Stderr, unit.Directory, report);
        const std::array<std::string_view, 4> stlKeys{"version", "__config", "__config_site", "__configuration/abi.h"};
        const std::array<std::string_view, 3> sdkKeys{"android/api-level.h", "android/ndk-version.h", "sys/cdefs.h"};
        if (!headersFromClang || !CheckClangHeaderOrigins(*headersFromClang, ndk.Include / "c++/v1", stlKeys, report) ||
            !CheckClangHeaderOrigins(*headersFromClang, ndk.Include, sdkKeys, report) ||
            DriverTriple(driver->Stderr) != "aarch64-none-linux-android29")
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_header_or_triple_origin"));
        }
        std::vector<std::string> baselineFlags{
            "-std=c++23",
            "-fno-exceptions",
            "--target=aarch64-none-linux-android29",
            "--sysroot=" + ndk.Sysroot.generic_string(),
        };
        if (Macro(macros->Stdout, "__GXX_RTTI").empty())
        {
            baselineFlags.emplace_back("-fno-rtti");
        }
        baselineFlags.insert(baselineFlags.end(), {"-E", "-dM", "-H", probe.generic_string()});
        const auto cleanConfig = Execute(request.Compiler, unit.Directory, std::move(baselineFlags), report);
        if (!cleanConfig || !CheckLibcxxConfiguration(macros->Stdout, cleanConfig->Stdout, report))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_libcxx_configuration"));
        }
        std::error_code originError;
        const auto selectedHeaders = std::filesystem::canonical(ndk.Include / "c++/v1", originError);
        const auto selectedSite = std::filesystem::canonical(ndk.Include / "c++/v1/__config_site", originError);
        if (originError ||
            !CheckLibcxxSourceConfiguration(unit.Source, *headersFromClang, selectedHeaders, selectedSite, report))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_libcxx_definitions"));
        }
        for (const auto& header : *headersFromClang)
        {
            observation.Headers.push_back(header.Spelling);
        }
        facts.at("units").push_back({
            {"source", unit.Source.generic_string()},
            {"object", unit.Object.generic_string()},
            {"macros", macros->Stdout},
            {"libcxx_baseline", cleanConfig->Stdout},
            {"driver", driver->Stderr},
            {"arguments", unit.AbiArguments},
            {"headers", macros->Stderr},
        });
    }
    // Link only a tiny independent witness with the *same* Android target and
    // sysroot; do not link the entire static Module or execute ARM64 on Windows.
    const auto binary = work / "runtime-witness";
    if (const auto safe = CheckObservationOutput(binary, report); !safe)
    {
        return std::unexpected(safe.error());
    }
    const std::vector<std::string> linkFlags{
        "--target=aarch64-none-linux-android29",
        "--sysroot=" + ndk.Sysroot.generic_string(),
        "-std=c++23",
        "-fno-exceptions",
        "-stdlib=libc++",
        witness.generic_string(),
        "-o",
        binary.generic_string(),
    };
    const auto linked = Execute(request.Compiler, request.BuildRoot, linkFlags, report);
    if (!linked || !std::filesystem::is_regular_file(binary))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_runtime_link"));
    }
    const auto readobj = request.Compiler.parent_path() / "llvm-readobj.exe";
    const auto needed = Execute(readobj, request.BuildRoot, {"--needed-libs", binary.generic_string()}, report);
    const auto header = Execute(readobj, request.BuildRoot, {"--file-headers", binary.generic_string()}, report);
    if (!needed || !header || !needed->Stdout.contains("libc++_shared.so") || !needed->Stdout.contains("libc.so") ||
        !header->Stdout.contains("EM_AARCH64"))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_runtime_needed_or_elf"));
    }
    facts.update(Json{{"runtime_needed", needed->Stdout}, {"runtime_elf", header->Stdout}});
    observation.Facts = std::move(facts);
    return (*ownership)->Finish(std::move(observation));
#endif
}

Result<CollectedProfile> CollectAndroidPlatformProfile(const CollectionRequest& request,
                                                       std::span<const CompileUnit> units,
                                                       const CompileObservation& observation, FailureReport& report)
{
    if (request.HostPlatform != Platform::kWindows || request.TargetPlatform != Platform::kAndroid || units.empty() ||
        !observation.Facts.is_object() || !observation.Facts.contains("units") ||
        observation.Facts.at("units").size() != units.size())
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_collection_inputs"));
    }
    const auto ndk = IdentifyNdk(request, report);
    if (!ndk)
    {
        return std::unexpected(ndk.error());
    }
    const auto& facts = observation.Facts;
    if (facts.value("sdk_id", "") != "android-ndk-r30-api-29" || facts.value("ndk_release", "") != "r30" ||
        facts.value("ndk_revision", "") != "30.0.16248370" ||
        facts.value("ndk_properties", "") != ndk->Properties.generic_string() ||
        facts.value("runtime_shared", "") != (ndk->Runtime / "libc++_shared.so").generic_string() ||
        facts.value("runtime_bionic", "") != (ndk->Runtime / "29/libc.so").generic_string() ||
        !facts.contains("runtime_needed") || !facts.at("runtime_needed").is_string() ||
        !facts.at("runtime_needed").get<std::string>().contains("libc++_shared.so") ||
        !facts.at("runtime_needed").get<std::string>().contains("libc.so") || !facts.contains("runtime_elf") ||
        !facts.at("runtime_elf").is_string() || !facts.at("runtime_elf").get<std::string>().contains("EM_AARCH64"))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_runtime_or_ndk_evidence_changed"));
    }
    const auto readobj = request.Compiler.parent_path() / "llvm-readobj.exe";
    std::optional<Json> baseline;
    for (std::size_t index = 0; index < units.size(); ++index)
    {
        const auto& unit = units.subspan(index).front();
        if (const auto checked = CheckLibcxxDriverArguments(unit.AbiArguments, report); !checked)
        {
            return std::unexpected(checked.error());
        }
        const auto& fact = facts.at("units").at(index);
        if (!std::filesystem::is_regular_file(unit.Object) ||
            fact.at("object").get<std::string>() != unit.Object.generic_string())
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_object_identity"));
        }
        if (const auto valid = CheckArguments(unit.AbiArguments, *ndk, report); !valid)
        {
            return std::unexpected(valid.error());
        }
        const auto header = Execute(readobj, unit.Directory, {"--file-headers", unit.Object.generic_string()}, report);
        if (!header || !header->Stdout.contains("EM_AARCH64"))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_object_elf_machine"));
        }
        const auto mapped = MapProfile(fact, facts, report);
        if (!mapped)
        {
            return std::unexpected(mapped.error());
        }
        if (baseline && *baseline != *mapped)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_tu_abi_disagreement"));
        }
        baseline = *mapped;
    }
    const auto schemaText = Required(request.SchemaFile, report);
    if (!baseline || !schemaText)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_schema_read"));
    }
    const auto schema = ParseUnique(*schemaText);
    if (!schema || !Validate(*schema, *baseline))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "android_frozen_schema"));
    }
    return CollectedProfile{
        .Profile = std::move(*baseline),
        .Evidence = observation.Facts,
        .ObservedInputs = observation.ObservedInputs,
    };
}
} // namespace pg::abi
