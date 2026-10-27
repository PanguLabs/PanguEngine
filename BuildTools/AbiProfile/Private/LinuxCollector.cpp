#include "LinuxCollector.h"

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
[[nodiscard]] Result<ProcessResult> Execute(const std::filesystem::path& executable,
                                            const std::filesystem::path& directory, std::vector<std::string> arguments,
                                            FailureReport& report)
{
    return ExecuteObservation(executable, directory, std::move(arguments), "linux_probe_exit", report);
}

[[nodiscard]] Result<std::string> RequiredFile(const std::filesystem::path& path, FailureReport& report)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error)
    {
        return std::unexpected(report.Add(ToolError::kReadInput, "linux_required_file", path.generic_string(), error));
    }
    return ReadFile(path, "linux_read_provenance", report);
}

[[nodiscard]] std::string_view Macro(std::string_view text, std::string_view key)
{
    const auto needle = std::string("#define ") + std::string(key) + " ";
    const auto start = text.find(needle);
    if (start == std::string_view::npos || (start != 0 && text.at(start - 1) != '\n'))
    {
        return {};
    }
    const auto offset = start + needle.size();
    const auto end = text.find('\n', offset);
    return text.substr(offset, end == std::string_view::npos ? end : end - offset);
}

[[nodiscard]] std::string Trim(std::string_view input)
{
    const auto first = input.find_first_not_of(" \r\n\t\"");
    if (first == std::string_view::npos)
    {
        return {};
    }
    const auto last = input.find_last_not_of(" \r\n\t\"");
    return std::string(input.substr(first, last - first + 1));
}

[[nodiscard]] std::string Triple(std::string_view driver)
{
    constexpr auto kStart = std::string_view(R"("-triple" ")");
    const auto pos = driver.find(kStart);
    if (pos == std::string_view::npos)
    {
        return {};
    }
    const auto begin = pos + kStart.size();
    const auto end = driver.find('"', begin);
    return end != std::string_view::npos ? std::string(driver.substr(begin, end - begin)) : std::string{};
}

[[nodiscard]] Result<std::string> LinuxSdkIdentity(FailureReport& report)
{
    const auto osRelease = RequiredFile("/etc/os-release", report);
    if (!osRelease)
    {
        return std::unexpected(osRelease.error());
    }
    std::string identifier;
    std::string version;
    for (std::size_t start = 0; start < osRelease->size();)
    {
        const auto end = osRelease->find('\n', start);
        const auto line = std::string_view(*osRelease).substr(start, end == std::string::npos ? end : end - start);
        if (line.starts_with("ID="))
        {
            identifier = Trim(line.substr(3));
        }
        if (line.starts_with("VERSION_ID="))
        {
            version = Trim(line.substr(11));
        }
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    // Native glibc/Ubuntu is the *only* currently verified Linux sysroot.
    // A different distribution or explicit sysroot requires a new backend.
    if (identifier != "ubuntu" || version != "24.04")
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_sysroot_unverified"));
    }
    return "linux-sysroot-" + identifier + "-" + version;
}

[[nodiscard]] Result<Json> ProfileForUnit(const Json& evidence, const Json& unit, std::string_view sdkId,
                                          FailureReport& report)
{
    const auto text = unit.at("macros").get<std::string>();
    if (!unit.contains("libcxx_baseline") || !unit.at("libcxx_baseline").is_string() ||
        !CheckLibcxxConfiguration(text, unit.at("libcxx_baseline").get_ref<const std::string&>(), report))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_libcxx_configuration"));
    }
    const auto version = Macro(text, "_LIBCPP_VERSION");
    const auto abiVersion = Macro(text, "_LIBCPP_ABI_VERSION");
    const auto abiNamespace = Macro(text, "_LIBCPP_ABI_NAMESPACE");
    const auto rtti = Macro(text, "__GXX_RTTI");
    const auto major = Macro(text, "__clang_major__");
    const auto minor = Macro(text, "__clang_minor__");
    const auto patch = Macro(text, "__clang_patchlevel__");
    if (major != "23" || version != "230100" || abiVersion != "1" || abiNamespace != "__1" ||
        Macro(text, "_LIBCPP_HARDENING_MODE") != "_LIBCPP_HARDENING_MODE_DEFAULT" ||
        Macro(text, "_LIBCPP_HARDENING_MODE_DEFAULT") != "2" || Macro(text, "_LIBCPP_ABI_FORCE_MICROSOFT") != "0" ||
        Macro(text, "_LIBCPP_ABI_FORCE_ITANIUM") != "0" || !Macro(text, "_LIBCPP_ABI_UNSTABLE").empty() ||
        !Macro(text, "_LIBCPP_ABI_ALTERNATE_STRING_LAYOUT").empty() || Macro(text, "__cplusplus") != "202302L" ||
        Macro(text, "__SIZEOF_POINTER__") != "8" || Macro(text, "__SIZEOF_WCHAR_T__") != "4" ||
        Macro(text, "__BYTE_ORDER__") != "__ORDER_LITTLE_ENDIAN__" || !Macro(text, "__CHAR_UNSIGNED__").empty() ||
        Macro(text, "__GLIBC__") != "2" || Macro(text, "__GLIBC_MINOR__") != "39" || (!rtti.empty() && rtti != "1") ||
        !Macro(text, "__EXCEPTIONS").empty() || !Macro(text, "__cpp_exceptions").empty() ||
        !Macro(text, "__AVX__").empty() || !Macro(text, "__SSE4_2__").empty())
    {
        return std::unexpected(
            report.Add(ToolError::kInvalidProfile, "linux_abi_macros", unit.at("source").get<std::string>()));
    }
    const auto triple = evidence.at("triple").get<std::string>();
    if (triple != "x86_64-unknown-linux-gnu" || Triple(unit.at("driver").get<std::string>()) != triple ||
        !evidence.at("compiler_banner")
             .get<std::string>()
             .contains("clang version " + std::string(major) + "." + std::string(minor) + "." + std::string(patch) +
                       " (https://github.com/llvm/llvm-project"))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_frontend"));
    }

    const auto& args = unit.at("arguments");
    if (!args.is_array())
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_compile_arguments"));
    }
    for (const auto& raw : args)
    {
        if (!raw.is_string())
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_compile_arguments"));
        }
        const auto arg = raw.get<std::string>();
        if (arg.starts_with("--sysroot") || arg.starts_with("-isysroot") || arg.starts_with("-march") ||
            arg.starts_with("-mcpu") || arg.starts_with("-mabi") || arg.starts_with("-fpack") ||
            arg.starts_with("-fshort-") || arg.starts_with("-D_LIBCPP_") || arg.starts_with("-U_LIBCPP_"))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_abi_options", arg));
        }
    }
    const auto compilerVersion = std::string(major) + "." + std::string(minor) + "." + std::string(patch);
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
                {"cpu_abi", "baseline-x86_64"},
            },
        },
        {
            "compiler",
            {{"id", "clang"}, {"distribution", "llvm-upstream"}, {"version", compilerVersion}, {"cxx_abi", "itanium"}},
        },
        {
            "language",
            {
                {"standard", "c++23"},
                {"exceptions", "off"},
                {"rtti", rtti.empty() ? "off" : "on"},
                {
                    "abi_options",
                    {
                        "bitfield_layout=itanium",
                        "calling_convention=sysv",
                        "char_signedness=signed",
                        "enum_width=native",
                        "float_abi=default",
                        "struct_pack=default",
                        "vector_abi=default",
                        "wchar_width=32",
                    },
                },
            },
        },
        {
            "stdlib",
            {
                {"id", "libc++"},
                {"version", "23.1.0"},
                {"abi_version", std::string(abiVersion)},
                {"abi_namespace", std::string(abiNamespace)},
                {"iterator_debug_level", "na"},
            },
        },
        {
            "runtime",
            {
                {"crt", "glibc"},
                {"cxx_runtime", "libcxx-shared"},
                {"cxxabi_runtime", "libcxxabi-shared"},
                {"sdk_abi_id", std::string(sdkId)},
            },
        },
    };
}
} // namespace

Result<CompileObservation> InspectLinuxCompileInputs(const CollectionRequest& request,
                                                     std::span<const CompileUnit> units, FailureReport& report)
{
#ifndef __linux__
    (void)request;
    (void)units;
    return std::unexpected(report.Add(ToolError::kInvalidRequest, "linux_requires_native_host"));
#else
    if (request.HostPlatform != Platform::kLinux || request.TargetPlatform != Platform::kLinux || units.empty())
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_target_not_supported"));
    }
    const auto compilerRoot = request.Compiler.parent_path().parent_path();
    const auto triple = std::string("x86_64-unknown-linux-gnu");
    const auto libcxxRoot = compilerRoot / "lib" / triple;
    const auto libcxxHeaders = compilerRoot / "include/c++/v1";
    const auto libcxxSite = compilerRoot / "include" / triple / "c++/v1";
    if (!RequiredFile(libcxxRoot / "libc++.so", report) || !RequiredFile(libcxxRoot / "libc++abi.so", report) ||
        !RequiredFile(libcxxHeaders / "version", report) || !RequiredFile(libcxxSite / "__config_site", report) ||
        !RequiredFile("/usr/include/features.h", report) || !RequiredFile("/lib/x86_64-linux-gnu/libc.so.6", report))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_runtime_sources"));
    }
    const auto distro = LinuxSdkIdentity(report);
    if (!distro)
    {
        return std::unexpected(distro.error());
    }
    const auto banner = Execute(request.Compiler, request.BuildRoot, {"--version"}, report);
    if (!banner)
    {
        return std::unexpected(banner.error());
    }
    const auto probe = request.SourceRoot / "BuildTools/AbiProfile/Probe/Libcxx.cpp";
    const auto witness = request.SourceRoot / "BuildTools/AbiProfile/Probe/PosixRuntimeWitness.cpp";
    const std::array knownInputs{probe, witness};
    const auto ownership = ObservationWork::Acquire(request, units, knownInputs, "linux_probe_exit", report);
    if (!ownership)
    {
        return std::unexpected(ownership.error());
    }
    const auto output = (*ownership)->Directory();
    if (!RequiredFile(probe, report) || !RequiredFile(witness, report))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_probe_source"));
    }
    CompileObservation observation;
    observation.ObservedInputs = {
        probe, witness, "/etc/os-release", "/usr/include/features.h", libcxxSite / "__config_site",
    };
    Json evidence{
        {"schema", 1},
        {"target", request.TargetName},
        {"compiler", request.Compiler.generic_string()},
        {"compiler_banner", banner->Stdout},
        {"triple", triple},
        {"sdk_id", *distro},
        {"stdlib_header", (libcxxHeaders / "version").generic_string()},
        {"runtime_libraries", Json::array()},
        {"units", Json::array()},
    };
    std::vector<std::string> witnessFlags;
    std::filesystem::path witnessWorking;
    for (std::size_t index = 0; index < units.size(); ++index)
    {
        const auto& unit = units.subspan(index).front();
        std::error_code compilerError;
        const bool sameCompiler = std::filesystem::equivalent(unit.Compiler, request.Compiler, compilerError);
        if (compilerError || !sameCompiler)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_compiler_identity"));
        }
        if (const auto checked = CheckLibcxxDriverArguments(unit.AbiArguments, report); !checked)
        {
            return std::unexpected(checked.error());
        }
        for (const auto& arg : unit.AbiArguments)
        {
            if (arg.starts_with("--sysroot") || arg.starts_with("-isysroot") || arg.starts_with("-march") ||
                arg.starts_with("-mcpu") || arg.starts_with("-mabi") || arg.starts_with("-fpack") ||
                arg.starts_with("-fshort-") || arg.starts_with("-D_LIBCPP_") || arg.starts_with("-U_LIBCPP_"))
            {
                return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_abi_options", arg));
            }
        }
        const auto observed = ObserveTranslationUnit(request, unit, probe, output, index, "linux_probe_exit", report);
        if (!observed)
        {
            return std::unexpected(observed.error());
        }
        const auto* macros = &observed->Macros;
        const auto* driver = &observed->Driver;
        const auto headers = ReadClangHeaderTrace(macros->Stderr, unit.Directory, report);
        const std::array<std::string_view, 3> stlKeys{"version", "__config", "__configuration/abi.h"};
        const std::array<std::string_view, 1> siteKeys{"__config_site"};
        const std::array<std::string_view, 1> sdkKeys{"features.h"};
        if (!headers || !CheckClangHeaderOrigins(*headers, libcxxHeaders, stlKeys, report) ||
            !CheckClangHeaderOrigins(*headers, libcxxSite, siteKeys, report) ||
            !CheckClangHeaderOrigins(*headers, "/usr/include", sdkKeys, report) || Triple(driver->Stderr) != triple)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_header_or_triple_conflict"));
        }
        std::vector<std::string> baselineFlags{
            "-std=c++23", "-fno-exceptions",           "-nostdinc++", "-isystem", libcxxHeaders.generic_string(),
            "-isystem",   libcxxSite.generic_string(),
        };
        if (Macro(macros->Stdout, "__GXX_RTTI").empty())
        {
            baselineFlags.emplace_back("-fno-rtti");
        }
        baselineFlags.insert(baselineFlags.end(), {"-E", "-dM", "-H", probe.generic_string()});
        const auto cleanConfig = Execute(request.Compiler, unit.Directory, std::move(baselineFlags), report);
        if (!cleanConfig || !CheckLibcxxConfiguration(macros->Stdout, cleanConfig->Stdout, report))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_libcxx_configuration"));
        }
        std::error_code originError;
        const auto selectedHeaders = std::filesystem::canonical(libcxxHeaders, originError);
        const auto selectedSite = std::filesystem::canonical(libcxxSite / "__config_site", originError);
        if (originError ||
            !CheckLibcxxSourceConfiguration(unit.Source, *headers, selectedHeaders, selectedSite, report))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_libcxx_definitions"));
        }
        for (const auto& header : *headers)
        {
            observation.Headers.push_back(header.Spelling);
        }
        evidence.at("units").push_back({
            {"source", unit.Source.generic_string()},
            {"object", unit.Object.generic_string()},
            {"driver", driver->Stderr},
            {"macros", macros->Stdout},
            {"libcxx_baseline", cleanConfig->Stdout},
            {"arguments", unit.AbiArguments},
        });
        if (index == 0)
        {
            witnessFlags = unit.AbiArguments;
            witnessWorking = unit.Directory;
        }
    }
    // The true linker used by CMake's Clang driver is authoritative here.
    // The installed ld.lld currently cannot launch due to missing libicu, so
    // do not force a synthetic '-fuse-ld=lld' that differs from CMake.
    const auto witnessBinary = output / "runtime-witness";
    if (const auto safe = CheckObservationOutput(witnessBinary, report); !safe)
    {
        return std::unexpected(safe.error());
    }
    auto args = witnessFlags;
    const auto runtimeDir = libcxxRoot.generic_string();
    args.insert(args.end(), {
                                witness.generic_string(),
                                "-stdlib=libc++",
                                "-L" + runtimeDir,
                                "-Wl,-rpath," + runtimeDir,
                                "-Wl,-t",
                                "-o",
                                witnessBinary.generic_string(),
                            });
    const auto linked = Execute(request.Compiler, witnessWorking, std::move(args), report);
    if (!linked || !std::filesystem::is_regular_file(witnessBinary))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_runtime_witness"));
    }
    const auto trace = linked->Stdout + "\n" + linked->Stderr;
    Json providers = Json::array();
    bool libcxx = false;
    bool abi = false;
    bool glibc = false;
    for (std::size_t cursor = 0; cursor < trace.size();)
    {
        const auto end = trace.find('\n', cursor);
        const auto line = Trim(std::string_view(trace).substr(cursor, end == std::string::npos ? end : end - cursor));
        if (line.starts_with('/') && std::filesystem::is_regular_file(line))
        {
            providers.push_back(line);
            const auto path = std::filesystem::path(line).lexically_normal().generic_string();
            if (path.starts_with(runtimeDir + "/") && path.ends_with("/libc++.so"))
            {
                libcxx = true;
            }
            if (path.starts_with(runtimeDir + "/") && path.ends_with("/libc++abi.so"))
            {
                abi = true;
            }
            if ((path == "/lib/x86_64-linux-gnu/libc.so.6" || path == "/usr/lib/x86_64-linux-gnu/libc.so.6"))
            {
                glibc = true;
            }
            observation.ObservedInputs.emplace_back(line);
        }
        if (end == std::string::npos)
        {
            break;
        }
        cursor = end + 1;
    }
    if (!libcxx || !abi || !glibc)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_runtime_providers"));
    }
    // Linker's verbose provider trace is not a substitute for the resulting
    // ELF dynamic dependency list: inspect the output without executing it.
    const auto readobj = request.Compiler.parent_path() / "llvm-readobj";
    const auto needed = Execute(readobj, witnessWorking, {"--needed-libs", witnessBinary.generic_string()}, report);
    if (!needed || !needed->Stdout.contains("libc++.so.1") || !needed->Stdout.contains("libc++abi.so.1") ||
        !needed->Stdout.contains("libc.so.6"))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_runtime_needed_libraries"));
    }
    evidence.at("runtime_libraries") = std::move(providers);
    evidence.update(Json{{"runtime_needed_libraries", needed->Stdout}});
    observation.Facts = std::move(evidence);
    return (*ownership)->Finish(std::move(observation));
#endif
}

Result<CollectedProfile> CollectLinuxPlatformProfile(const CollectionRequest& request,
                                                     std::span<const CompileUnit> units,
                                                     const CompileObservation& observation, FailureReport& report)
{
    if (request.TargetPlatform != Platform::kLinux || !observation.Facts.is_object() || units.empty() ||
        !observation.Facts.contains("units") || observation.Facts.at("units").size() != units.size())
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_collection_inputs"));
    }
    // Snapshot facts are not an authority by themselves. Revalidate SDK
    // identity and runtime-provider files at the final collection boundary.
    const auto observedSdk = LinuxSdkIdentity(report);
    const auto* claimedSdk = Find(observation.Facts, "sdk_id");
    const auto* providers = Find(observation.Facts, "runtime_libraries");
    const auto* needed = Find(observation.Facts, "runtime_needed_libraries");
    if (!observedSdk || claimedSdk == nullptr || !claimedSdk->is_string() || *claimedSdk != *observedSdk ||
        providers == nullptr || !providers->is_array() || providers->empty() || needed == nullptr ||
        !needed->is_string() || !needed->get<std::string>().contains("libc++.so.1") ||
        !needed->get<std::string>().contains("libc++abi.so.1") || !needed->get<std::string>().contains("libc.so.6"))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_sysroot_evidence_changed"));
    }
    const auto runtimeRoot =
        (request.Compiler.parent_path().parent_path() / "lib/x86_64-unknown-linux-gnu").generic_string() + "/";
    bool sawLibcxx = false;
    bool sawLibcxxAbi = false;
    bool sawGlibc = false;
    for (const auto& file : *providers)
    {
        if (!file.is_string())
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_runtime_evidence_type"));
        }
        const auto path = file.get<std::string>();
        std::error_code providerError;
        if (!std::filesystem::is_regular_file(path, providerError) || providerError)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_runtime_evidence_missing", path));
        }
        const auto filePath = std::filesystem::path(path).lexically_normal().generic_string();
        const bool installedLibcxx = filePath.starts_with(runtimeRoot);
        const bool distroRuntime =
            filePath.starts_with("/usr/lib/") || filePath.starts_with("/lib/") || filePath.starts_with("/lib64/");
        if (!installedLibcxx && !distroRuntime)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_runtime_evidence_origin", path));
        }
        sawLibcxx |= installedLibcxx && filePath.ends_with("/libc++.so");
        sawLibcxxAbi |= installedLibcxx && filePath.ends_with("/libc++abi.so");
        sawGlibc |= filePath == "/lib/x86_64-linux-gnu/libc.so.6" || filePath == "/usr/lib/x86_64-linux-gnu/libc.so.6";
    }
    if (!sawLibcxx || !sawLibcxxAbi || !sawGlibc)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_runtime_evidence_incomplete"));
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
        const auto& fact = observation.Facts.at("units").at(index);
        if (!std::filesystem::is_regular_file(unit.Object) ||
            fact.at("object").get<std::string>() != unit.Object.generic_string())
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_object_identity"));
        }
        const auto inspected =
            Execute(readobj, unit.Directory, {"--file-headers", unit.Object.generic_string()}, report);
        if (!inspected || !inspected->Stdout.contains("elf64-x86-64"))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_object_format"));
        }
        const auto mapped =
            ProfileForUnit(observation.Facts, fact, observation.Facts.at("sdk_id").get<std::string>(), report);
        if (!mapped)
        {
            return std::unexpected(mapped.error());
        }
        if (baseline && *baseline != *mapped)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_tu_disagreement"));
        }
        baseline = *mapped;
    }
    if (!baseline)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_no_translation_units"));
    }
    const auto raw = RequiredFile(request.SchemaFile, report);
    if (!raw)
    {
        return std::unexpected(raw.error());
    }
    const auto schema = ParseUnique(*raw);
    if (!schema || !Validate(*schema, *baseline))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "linux_frozen_schema"));
    }
    return CollectedProfile{
        .Profile = std::move(*baseline),
        .Evidence = observation.Facts,
        .ObservedInputs = observation.ObservedInputs,
    };
}
} // namespace pg::abi
