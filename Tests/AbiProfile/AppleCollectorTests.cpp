#include "../../BuildTools/AbiProfile/Private/AppleCollector.h"
#include "../../BuildTools/AbiProfile/Private/CollectionRequest.h"
#include "../../BuildTools/AbiProfile/Private/CompileCommands.h"
#include "../../BuildTools/AbiProfile/Private/HeaderTrace.h"
#include "../../BuildTools/AbiProfile/Private/Process.h"
#include "../../BuildTools/AbiProfile/Private/ToolIo.h"
#include "../../BuildTools/AbiProfile/Private/ToolReport.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

int main(int argc, char* const argv[])
{
#ifndef __APPLE__
    (void)argc;
    (void)argv;
    return 0;
#else
    if (argc != 5)
    {
        return 2;
    }
    const auto args = std::span(argv, static_cast<std::size_t>(argc));
    const auto root = std::filesystem::path(args.subspan<1>().front());
    const auto build = std::filesystem::path(args.subspan<2>().front());
    const auto compiler = std::filesystem::path(args.subspan<3>().front());
    const auto platform = std::string(args.subspan<4>().front());
    if (platform != "macos" && platform != "ios")
    {
        return 2;
    }
    pg::abi::FailureReport report;
    const auto data = pg::abi::ReadFile(build / "compile_commands.json", "apple_database", report);
    if (!data)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    const auto commands = nlohmann::json::parse(*data, nullptr, false);
    if (!commands.is_array())
    {
        return 1;
    }
    std::string objects;
    for (const auto& entry : commands)
    {
        if (!entry.is_object() || !entry.contains("file") || !entry.contains("output"))
        {
            continue;
        }
        const auto filename = entry.at("file").get<std::string>();
        if (filename.ends_with("/Source/Runtime/Foundation/Private/Error.cpp") ||
            filename.ends_with("/Source/Runtime/Module/Private/ModuleIdentity.cpp"))
        {
            objects += entry.at("output").get<std::string>() + "\n";
        }
    }
    if (std::ranges::count(objects, '\n') != 2)
    {
        std::cerr << "Apple source/object manifest did not select two real TUs\n";
        return 1;
    }
    const auto out = build / "Tests/AbiProfile/AppleCollectorCases";
    std::error_code error;
    std::filesystem::create_directories(out, error);
    if (error || !pg::abi::WriteFile(out / "objects.txt", objects, report))
    {
        return 1;
    }
    pg::abi::CollectionRequest request;
    request.SourceRoot = root;
    request.BuildRoot = build;
    request.Compiler = compiler;
    request.Database = build / "compile_commands.json";
    request.ObjectManifest = out / "objects.txt";
    request.TargetName = "AppleCollectorCase";
    request.Configuration = "Release";
    request.HostPlatform = pg::abi::Platform::kMacos;
    request.TargetPlatform = platform == "macos" ? pg::abi::Platform::kMacos : pg::abi::Platform::kIos;
    request.CommandLineStyle = pg::abi::CommandLineStyle::kPosix;
    request.OutputDirectory = out / platform;
    request.SchemaFile = root / "wiki/foundation/abi-profile-v1.schema.json";
    request.Library = build / "Lib/libPanguFoundation.a";
    request.SignatureHeader = request.OutputDirectory / "inputs.h";
    request.LinkGraph = out / "links.json";
    request.PlatformContext = nlohmann::json::object();
    const auto units = pg::abi::LoadCompileUnits(request, report);
    if (!units || units->size() != 2)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    // A configured MacPorts installation may use a different prefix. A real
    // copied driver and shared installed headers/resources model that prefix.
    const auto relocated = out / "configured-llvm-prefix";
    std::filesystem::create_directories(relocated / "bin", error);
    std::filesystem::copy_file(compiler, relocated / "bin/clang++", std::filesystem::copy_options::overwrite_existing,
                               error);
    if (error)
    {
        std::cerr << "Cannot prepare relocated LLVM driver: " << error.message() << '\n';
        return 1;
    }
    for (const auto* directory : {"lib", "include"})
    {
        if (!std::filesystem::exists(relocated / directory))
        {
            std::filesystem::create_directory_symlink(compiler.parent_path().parent_path() / directory,
                                                      relocated / directory, error);
        }
        if (error)
        {
            return 1;
        }
    }
    if (!std::filesystem::exists(relocated / "bin/llvm-readobj"))
    {
        std::filesystem::create_symlink(compiler.parent_path() / "llvm-readobj", relocated / "bin/llvm-readobj", error);
        if (error)
        {
            return 1;
        }
    }
    auto relocatedRequest = request;
    relocatedRequest.Compiler = relocated / "bin/clang++";
    auto relocatedUnits = *units;
    for (auto& unit : relocatedUnits)
    {
        unit.Compiler = relocatedRequest.Compiler;
    }
    pg::abi::FailureReport relocatedReport;
    if (!pg::abi::InspectAppleCompileInputs(relocatedRequest, relocatedUnits, relocatedReport))
    {
        std::cerr << "Collector rejected the configured LLVM installation prefix\n";
        relocatedReport.WriteJson(std::cerr);
        return 1;
    }
    const auto observation = pg::abi::InspectAppleCompileInputs(request, *units, report);
    if (!observation)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    // The runtime witness linker's LC_UUID is a non-ABI identifier and
    // changes across identical probe links. Compile inputs must be stable.
    const auto repeatedObservation = pg::abi::InspectAppleCompileInputs(request, *units, report);
    const auto& machoBuild = observation->Facts.at("runtime_macho_build").get_ref<const std::string&>();
    if (!repeatedObservation || repeatedObservation->Facts != observation->Facts ||
        (machoBuild.contains("cmd LC_UUID") && !machoBuild.contains("uuid <link-generated>")))
    {
        std::cerr << "Apple identical compile inputs produced unstable linker UUID evidence\n";
        report.WriteJson(std::cerr);
        return 1;
    }
    const auto profile = pg::abi::CollectApplePlatformProfile(request, *units, *observation, report);
    if (!profile || profile->Profile.at("target").at("object") != "macho" ||
        profile->Profile.at("stdlib").at("abi_namespace") != "__1" ||
        profile->Profile.at("compiler").at("distribution") != "macports" ||
        profile->Profile.at("runtime").at("sdk_abi_id") != (platform == "macos" ? "macos-sdk-15.2" : "ios-sdk-18.2"))
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
        const auto seen = pg::abi::InspectAppleCompileInputs(request, changed, failure);
        if (seen && pg::abi::CollectApplePlatformProfile(request, changed, *seen, failure))
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
    const auto ordinarySeen = pg::abi::InspectAppleCompileInputs(request, ordinary, report);
    const auto ordinaryProfile = ordinarySeen
                                     ? pg::abi::CollectApplePlatformProfile(request, ordinary, *ordinarySeen, report)
                                     : pg::Result<pg::abi::CollectedProfile>{};
    if (!ordinaryProfile || ordinaryProfile->Profile != profile->Profile)
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
    // REVIEW_REGRESSION_BEGIN: -H header trace must enter snapshot.
    if (observation->Headers.empty())
    {
        std::cerr << "Apple Collector did not snapshot real transitive headers\n";
        return 1;
    }
    // REVIEW_REGRESSION_END.
    // REVIEW_REGRESSION_BEGIN: unsigned-char macro must not pass as signed.
    auto unsignedFacts = *observation;
    auto& unsignedMacroText = unsignedFacts.Facts.at("units").at(1).at("macros");
    unsignedMacroText = unsignedMacroText.get<std::string>() + "\n#define __CHAR_UNSIGNED__ 1\n";
    pg::abi::FailureReport unsignedReport;
    if (pg::abi::CollectApplePlatformProfile(request, *units, unsignedFacts, unsignedReport))
    {
        std::cerr << "Collector accepted unsigned-char ABI but reported signed\n";
        return 1;
    }
    // REVIEW_REGRESSION_END.
    // Actual char semantics, not just potentially overridden built-in macros.
    auto compilerMacroSpoof = *units;
    compilerMacroSpoof.at(1).AbiArguments.emplace_back("-funsigned-char");
    compilerMacroSpoof.at(1).AbiArguments.emplace_back("-U__CHAR_UNSIGNED__");
    pg::abi::FailureReport spoofReport;
    const auto spoofObservation = pg::abi::InspectAppleCompileInputs(request, compilerMacroSpoof, spoofReport);
    if (spoofObservation)
    {
        pg::abi::FailureReport mappingReport;
        if (pg::abi::CollectApplePlatformProfile(request, compilerMacroSpoof, *spoofObservation, mappingReport))
        {
            std::cerr << "Collector accepted char semantics with spoofed built-in macro\n";
            return 1;
        }
    }
    const auto rejectFacts = [&](std::string_view reason, const auto& mutate)
    {
        auto altered = *observation;
        mutate(altered.Facts);
        pg::abi::FailureReport invalid;
        if (pg::abi::CollectApplePlatformProfile(request, *units, altered, invalid))
        {
            std::cerr << "Apple accepted forged provenance: " << reason << '\n';
            return false;
        }
        return true;
    };
    if (!rejectFacts("SDK version", [](auto& facts) { facts.at("sdk_version") = "99.9"; }) ||
        !rejectFacts("Runtime needed libraries", [](auto& facts) { facts.at("runtime_needed") = "libSystem only"; }) ||
        !rejectFacts("Mach-O runtime target",
                     [](auto& facts) { facts.at("runtime_macho_build") = "platform 0\nsdk 0.0"; }) ||
        !rejectFacts("libc++ ABI namespace",
                     [](auto& facts)
                     {
                         auto text = facts.at("units").at(1).at("macros").template get<std::string>();
                         constexpr std::string_view kBefore = "#define _LIBCPP_ABI_NAMESPACE __1";
                         const auto index = text.find(kBefore);
                         if (index != std::string::npos)
                         {
                             text.replace(index, kBefore.size(), "#define _LIBCPP_ABI_NAMESPACE __2");
                         }
                         facts.at("units").at(1).at("macros") = std::move(text);
                     }))
    {
        return 1;
    }
    auto wrongSdk = *units;
    wrongSdk.at(1).AbiArguments.insert(wrongSdk.at(1).AbiArguments.end(), {"-isysroot", "/tmp/unknown-Apple-SDK"});
    pg::abi::FailureReport invalidOptions;
    if (pg::abi::InspectAppleCompileInputs(request, wrongSdk, invalidOptions))
    {
        std::cerr << "Apple accepted an unverified additional SDK\n";
        return 1;
    }
    // Missing SDKSettings and alternative compilation triples must fail in
    // the same C++ backend that handles the successful toolchain.
    auto unknownSdk = *units;
    auto& options = unknownSdk.at(1).AbiArguments;
    const auto sdkIt = std::ranges::find(options, "-isysroot");
    if (sdkIt == options.end() || std::next(sdkIt) == options.end())
    {
        return 1;
    }
    *std::next(sdkIt) = "/tmp/PanguMissingAppleSDK";
    pg::abi::FailureReport missingSdkReport;
    if (pg::abi::InspectAppleCompileInputs(request, unknownSdk, missingSdkReport))
    {
        std::cerr << "Apple accepted missing SDKSettings.json / SDK Runtime provider\n";
        return 1;
    }
    auto layoutChange = *units;
    layoutChange.at(1).AbiArguments.emplace_back("-fpack-struct=1");
    pg::abi::FailureReport packingReport;
    if (pg::abi::InspectAppleCompileInputs(request, layoutChange, packingReport))
    {
        std::cerr << "Apple accepted an unknown compiler ABI layout override\n";
        return 1;
    }
    // Security fixtures require native symlink and hard-link semantics.
    // Keep the disposable files in a private local scratch directory.
    const auto scratch = std::filesystem::temp_directory_path() / ("pangu-apple-safe-output-" + platform);
    std::filesystem::remove_all(scratch, error);
    if (error)
    {
        return 1;
    }
    const auto target = scratch / "redirect";
    const auto localOutput = scratch / "result";
    std::filesystem::create_directories(target, error);
    if (error)
    {
        return 1;
    }
    std::filesystem::create_directories(localOutput, error);
    if (error)
    {
        return 1;
    }
    auto localRequest = request;
    localRequest.OutputDirectory = localOutput;
    std::filesystem::create_directory_symlink(target, localOutput / "work", error);
    if (error)
    {
        std::cerr << "Local Apple filesystem cannot create security symlink: " << error.message() << '\n';
        return 1;
    }
    pg::abi::FailureReport symlinkReport;
    if (pg::abi::InspectAppleCompileInputs(localRequest, *units, symlinkReport) ||
        std::filesystem::exists(target / "unit-0.cpp"))
    {
        std::cerr << "Apple Collector followed a symlinked output directory\n";
        return 1;
    }
    std::filesystem::remove(localOutput / "work", error);
    std::filesystem::create_directories(localOutput / "work", error);
    const auto protectedInput = scratch / "source-preserved.txt";
    const auto wrapperPath = localOutput / "work/unit-0.cpp";
    if (error || !pg::abi::WriteFile(protectedInput, "unchanged", report))
    {
        return 1;
    }
    std::filesystem::create_hard_link(protectedInput, wrapperPath, error);
    if (error)
    {
        return 1;
    }
    pg::abi::FailureReport hardlinkReport;
    const auto unsafe = pg::abi::InspectAppleCompileInputs(localRequest, *units, hardlinkReport);
    const auto input = pg::abi::ReadFile(protectedInput, "protected", report);
    if (unsafe || !input || *input != "unchanged")
    {
        std::cerr << "Apple Collector overwrote a hardlinked external input\n";
        return 1;
    }
    std::filesystem::remove_all(scratch, error);
    if (error)
    {
        return 1;
    }
    return 0;
#endif
}
