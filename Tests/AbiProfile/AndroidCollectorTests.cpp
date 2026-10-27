#include "../../BuildTools/AbiProfile/Private/AndroidCollector.h"
#include "../../BuildTools/AbiProfile/Private/CollectionRequest.h"
#include "../../BuildTools/AbiProfile/Private/CompileCommands.h"
#include "../../BuildTools/AbiProfile/Private/HeaderTrace.h"
#include "../../BuildTools/AbiProfile/Private/Process.h"
#include "../../BuildTools/AbiProfile/Private/ToolIo.h"
#include "../../BuildTools/AbiProfile/Private/ToolReport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

int main(int argc, char* const argv[])
{
#ifndef _WIN32
    (void)argc;
    (void)argv;
    return 0;
#else
    if (argc != 5)
    {
        return 2;
    }
    const auto arguments = std::span(argv, static_cast<std::size_t>(argc));
    const auto sourceRoot = std::filesystem::path(arguments.subspan<1>().front());
    const auto androidBuild = std::filesystem::path(arguments.subspan<2>().front());
    const auto compiler = std::filesystem::path(arguments.subspan<3>().front());
    const auto testOutput = std::filesystem::path(arguments.subspan<4>().front());
    pg::abi::FailureReport report;
    const auto text = pg::abi::ReadFile(androidBuild / "compile_commands.json", "android_database", report);
    if (!text)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    const auto database = nlohmann::json::parse(*text, nullptr, false);
    if (!database.is_array())
    {
        return 1;
    }
    std::string objects;
    for (const auto& row : database)
    {
        if (!row.contains("file") || !row.contains("output"))
        {
            continue;
        }
        const auto filename = row.at("file").get<std::string>();
        if (filename.ends_with("/Source/Runtime/Foundation/Private/Error.cpp") ||
            filename.ends_with("/Source/Runtime/Module/Private/ModuleIdentity.cpp"))
        {
            objects += row.at("output").get<std::string>() + "\n";
        }
    }
    if (std::ranges::count(objects, '\n') != 2)
    {
        std::cerr << "Expected two actual Android ARM64 Runtime TUs\n";
        return 1;
    }
    std::error_code error;
    std::filesystem::create_directories(testOutput, error);
    if (error || !pg::abi::WriteFile(testOutput / "objects.txt", objects, report))
    {
        return 1;
    }
    pg::abi::CollectionRequest request;
    request.SourceRoot = sourceRoot;
    request.BuildRoot = androidBuild;
    request.Compiler = compiler;
    request.Database = androidBuild / "compile_commands.json";
    request.ObjectManifest = testOutput / "objects.txt";
    request.TargetName = "AndroidCollectorFixture";
    request.Configuration = "Release";
    request.HostPlatform = pg::abi::Platform::kWindows;
    request.TargetPlatform = pg::abi::Platform::kAndroid;
    request.CommandLineStyle = pg::abi::CommandLineStyle::kWindows;
    request.OutputDirectory = testOutput / "profiles";
    request.SchemaFile = sourceRoot / "wiki/foundation/abi-profile-v1.schema.json";
    request.Library = androidBuild / "Lib/libPanguFoundation.a";
    request.SignatureHeader = request.OutputDirectory / "inputs.h";
    request.LinkGraph = testOutput / "links.json";
    request.PlatformContext = nlohmann::json::object();
    const auto units = pg::abi::LoadCompileUnits(request, report);
    if (!units || units->size() != 2)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    const auto observation = pg::abi::InspectAndroidCompileInputs(request, *units, report);
    if (!observation)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    const auto repeated = pg::abi::InspectAndroidCompileInputs(request, *units, report);
    if (!repeated || repeated->Facts != observation->Facts || repeated->Headers != observation->Headers ||
        repeated->ObservedInputs != observation->ObservedInputs)
    {
        std::cerr << "Private observation paths changed stable compile evidence\n";
        report.WriteJson(std::cerr);
        return 1;
    }
    const auto collected = pg::abi::CollectAndroidPlatformProfile(request, *units, *observation, report);
    if (!collected || collected->Profile.at("runtime").at("sdk_abi_id") != "android-ndk-r30-api-29" ||
        collected->Profile.at("stdlib").at("abi_namespace") != "__ndk1" ||
        collected->Profile.at("target").at("cpu_abi") != "aapcs64")
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    // Regression: real source-local libc++ modes and driver ABI controls.
    bool acceptedReviewInput = false;
    const auto reviewSource = request.OutputDirectory / "ReviewAbi.cpp";
    const auto rejectReviewInput =
        [&](std::string_view name, std::string_view text, const std::vector<std::string>& flags)
    {
        if (!pg::abi::WriteFile(reviewSource, text, report))
        {
            return false;
        }
        auto changed = *units;
        changed.at(1).Source = reviewSource;
        changed.at(1).AbiArguments.insert(changed.at(1).AbiArguments.end(), flags.begin(), flags.end());
        pg::abi::FailureReport failure;
        const auto seen = pg::abi::InspectAndroidCompileInputs(request, changed, failure);
        if (seen && pg::abi::CollectAndroidPlatformProfile(request, changed, *seen, failure))
        {
            std::cerr << "Accepted unsupported real ABI input: " << name << '\n';
            acceptedReviewInput = true;
        }
        return true;
    };
    if (!rejectReviewInput("source bounded vector iterators",
                           "#define _LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR\n#include <vector>\n", {}) ||
        !rejectReviewInput("source restored after STL use",
                           "#define _LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR\n#include <vector>\n"
                           "#undef _LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR\n",
                           {}) ||
        !rejectReviewInput("old Clang ABI compatibility", "#include <vector>\n", {"-fclang-abi-compat=4"}))
    {
        return 1;
    }
    const auto overlay = request.OutputDirectory / "overlay";
    std::filesystem::create_directories(overlay, error);
    if (error || !pg::abi::WriteFile(overlay / "version", "#include_next <version>\n", report) ||
        !pg::abi::WriteFile(reviewSource, "#include <version>\n", report))
    {
        return 1;
    }
    pg::abi::ProcessRequest search;
    search.Program = compiler;
    search.Directory = units->front().Directory;
    search.Arguments = units->front().AbiArguments;
    search.Arguments.insert(search.Arguments.end(),
                            {"-I" + overlay.generic_string(), "-E", "-H", reviewSource.generic_string()});
    const auto traceReview = pg::abi::RunProcess(search, report);
    const auto headersReview = traceReview && traceReview->ExitCode == 0
                                   ? pg::abi::ParseClangHeaderTrace(traceReview->Stderr, search.Directory, report)
                                   : pg::Result<std::vector<std::filesystem::path>>{};
    if (!headersReview ||
        std::ranges::find(*headersReview, std::filesystem::canonical(overlay / "version")) == headersReview->end())
    {
        std::cerr << "Overlay characterization did not reach the real Clang header search\n";
        return 1;
    }
    if (!pg::abi::WriteFile(reviewSource, "#define REVIEW_PROJECT_FEATURE 1\n#include <vector>\n", report))
    {
        return 1;
    }
    auto ordinary = *units;
    ordinary.at(1).Source = reviewSource;
    ordinary.at(1).AbiArguments.insert(ordinary.at(1).AbiArguments.end(),
                                       {"-DREVIEW_PROJECT_FLAG=1", "-g", "-fno-omit-frame-pointer"});
    const auto ordinarySeen = pg::abi::InspectAndroidCompileInputs(request, ordinary, report);
    const auto ordinaryProfile = ordinarySeen
                                     ? pg::abi::CollectAndroidPlatformProfile(request, ordinary, *ordinarySeen, report)
                                     : pg::Result<pg::abi::CollectedProfile>{};
    if (!ordinaryProfile || ordinaryProfile->Profile != collected->Profile)
    {
        std::cerr << "Ordinary project macros or debug flags were incorrectly rejected\n";
        report.WriteJson(std::cerr);
        return 1;
    }
    if (!rejectReviewInput("mixed version overlay", "#include <version>\n", {"-I" + overlay.generic_string()}) ||
        acceptedReviewInput)
    {
        return 1;
    }
    // REVIEW_REGRESSION_BEGIN: real -I headers must be content-snapshotted.
    const auto relativeDir = units->front().Directory / "pangu-review-relative-headers";
    std::error_code relativeError;
    std::filesystem::create_directories(relativeDir, relativeError);
    if (relativeError)
    {
        return 1;
    }
    const auto relativeHeader = relativeDir / "ReviewIncluded.h";
    if (!pg::abi::WriteFile(relativeHeader, "#pragma once\n#define PANGU_REVIEW_HEADER 1\n", report))
    {
        return 1;
    }
    const auto probeSource = relativeDir / "ReviewSource.cpp";
    if (!pg::abi::WriteFile(probeSource, "#include <ReviewIncluded.h>\n", report))
    {
        return 1;
    }
    pg::abi::ProcessRequest process;
    process.Program = request.Compiler;
    process.Directory = units->front().Directory;
    process.Arguments = units->front().AbiArguments;
    process.Arguments.insert(process.Arguments.end(),
                             {"-Ipangu-review-relative-headers", "-E", "-dM", "-H", probeSource.generic_string()});
    pg::abi::FailureReport relativeReport;
    const auto trace = pg::abi::RunProcess(process, relativeReport);
    const auto parsed = trace && trace->ExitCode == 0
                            ? pg::abi::ParseClangHeaderTrace(trace->Stderr, process.Directory, relativeReport)
                            : pg::Result<std::vector<std::filesystem::path>>{std::unexpected(
                                  relativeReport.Add(pg::abi::ToolError::kInvalidProfile, "header_test_compiler"))};
    if (!parsed || std::ranges::none_of(*parsed,
                                        [&](const std::filesystem::path& path)
                                        {
                                            std::error_code equalError;
                                            return std::filesystem::equivalent(path, relativeHeader, equalError) &&
                                                   !equalError;
                                        }))
    {
        std::cerr << "Relative Clang -H header was not captured by the common parser\n";
        relativeReport.WriteJson(std::cerr);
        return 1;
    }
    pg::abi::FailureReport missingHeaderReport;
    if (pg::abi::ParseClangHeaderTrace(". pangu-review-relative-headers/review-missing-header.h\n", process.Directory,
                                       missingHeaderReport))
    {
        std::cerr << "Collector header trace silently ignored a missing reported header\n";
        return 1;
    }
    // REVIEW_REGRESSION_END.
    // Actual char semantics, not just potentially overridden built-in macros.
    auto compilerMacroSpoof = *units;
    compilerMacroSpoof.at(1).AbiArguments.emplace_back("-fsigned-char");
    compilerMacroSpoof.at(1).AbiArguments.emplace_back("-D__CHAR_UNSIGNED__=1");
    pg::abi::FailureReport spoofReport;
    const auto spoofObservation = pg::abi::InspectAndroidCompileInputs(request, compilerMacroSpoof, spoofReport);
    if (spoofObservation)
    {
        pg::abi::FailureReport mappingReport;
        if (pg::abi::CollectAndroidPlatformProfile(request, compilerMacroSpoof, *spoofObservation, mappingReport))
        {
            std::cerr << "Collector accepted char semantics with spoofed built-in macro\n";
            return 1;
        }
    }
    const auto rejects = [&](std::string_view reason, const auto& alter)
    {
        auto forged = *observation;
        alter(forged.Facts);
        pg::abi::FailureReport failure;
        if (pg::abi::CollectAndroidPlatformProfile(request, *units, forged, failure))
        {
            std::cerr << "Accepted forged Android ABI evidence: " << reason << '\n';
            return false;
        }
        return true;
    };
    const auto replaceMacro = [](auto& facts, std::string_view before, std::string_view after)
    {
        auto value = facts.at("units").at(1).at("macros").template get<std::string>();
        const auto index = value.find(before);
        if (index != std::string::npos)
        {
            value.replace(index, before.size(), after);
        }
        facts.at("units").at(1).at("macros") = std::move(value);
    };
    if (!rejects("NDK ReleaseName", [](auto& facts) { facts.at("ndk_release") = "r29"; }) ||
        !rejects("NDK revision", [](auto& facts) { facts.at("ndk_revision") = "29.0.0"; }) ||
        !rejects("SDK id", [](auto& facts) { facts.at("sdk_id") = "android-ndk-r29-api-29"; }) ||
        !rejects("Runtime library", [](auto& facts) { facts.at("runtime_needed") = "libc.so"; }) ||
        !rejects("ELF machine", [](auto& facts) { facts.at("runtime_elf") = "EM_X86_64"; }) ||
        !rejects(
            "libc++ ABI namespace", [&](auto& facts)
            { replaceMacro(facts, "#define _LIBCPP_ABI_NAMESPACE __ndk1", "#define _LIBCPP_ABI_NAMESPACE __1"); }) ||
        !rejects("unsigned char ABI", [&](auto& facts)
                 { replaceMacro(facts, "#define __CHAR_UNSIGNED__ 1", "#define __CHAR_UNSIGNED__ 0"); }) ||
        !rejects("API level",
                 [&](auto& facts)
                 {
                     replaceMacro(facts, "#define __ANDROID_MIN_SDK_VERSION__ 29",
                                  "#define __ANDROID_MIN_SDK_VERSION__ 28");
                 }))
    {
        return 1;
    }
    auto conflictingTarget = *units;
    conflictingTarget.at(1).AbiArguments.emplace_back("--target=aarch64-none-linux-android28");
    pg::abi::FailureReport apiError;
    if (pg::abi::InspectAndroidCompileInputs(request, conflictingTarget, apiError))
    {
        std::cerr << "Android collector accepted a changed API target\n";
        return 1;
    }
    auto malformedPacking = *units;
    malformedPacking.at(1).AbiArguments.emplace_back("-fpack-struct=1");
    pg::abi::FailureReport packingError;
    if (pg::abi::InspectAndroidCompileInputs(request, malformedPacking, packingError))
    {
        std::cerr << "Android collector accepted unsupported struct packing\n";
        return 1;
    }
    // A hardlink must never cause probe generation to overwrite another file.
    const auto protectedInput = testOutput / "protected-source.txt";
    const auto wrapper = request.OutputDirectory / "work/unit-0.cpp";
    if (!pg::abi::WriteFile(protectedInput, "preserve android fixture", report))
    {
        return 1;
    }
    std::filesystem::remove(wrapper, error);
    if (error)
    {
        return 1;
    }
    std::filesystem::create_hard_link(protectedInput, wrapper, error);
    if (error)
    {
        return 1;
    }
    pg::abi::FailureReport hardlinkReport;
    const auto protectedObservation = pg::abi::InspectAndroidCompileInputs(request, *units, hardlinkReport);
    const auto after = pg::abi::ReadFile(protectedInput, "protected_input", report);
    std::filesystem::remove(wrapper, error);
    if (protectedObservation || !after || *after != "preserve android fixture" || error)
    {
        std::cerr << "Android collector overwrote a hardlinked probe input\n";
        return 1;
    }
    return 0;
#endif
}
