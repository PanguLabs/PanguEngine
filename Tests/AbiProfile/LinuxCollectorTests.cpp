#include "../../Tools/AbiProfile/Private/CollectionRequest.h"
#include "../../Tools/AbiProfile/Private/CollectionState.h"
#include "../../Tools/AbiProfile/Private/CompileCommands.h"
#include "../../Tools/AbiProfile/Private/HeaderTrace.h"
#include "../../Tools/AbiProfile/Private/LinuxCollector.h"
#include "../../Tools/AbiProfile/Private/Process.h"
#include "../../Tools/AbiProfile/Private/ToolIo.h"
#include "../../Tools/AbiProfile/Private/ToolReport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <system_error>
#include <vector>

int main(int argc, char* const argv[])
{
#ifndef __linux__
    (void)argc;
    (void)argv;
    return 0;
#else
    if (argc != 4)
    {
        return 2;
    }
    const auto args = std::span(argv, static_cast<std::size_t>(argc));
    const auto root = std::filesystem::path(args.subspan<1>().front());
    const auto build = std::filesystem::path(args.subspan<2>().front());
    const auto compiler = std::filesystem::path(args.subspan<3>().front());
    pg::abi::FailureReport report;
    const auto source = pg::abi::ReadFile(build / "compile_commands.json", "linux_database", report);
    if (!source)
    {
        return 1;
    }
    const auto rows = nlohmann::json::parse(*source, nullptr, false);
    if (!rows.is_array())
    {
        return 1;
    }
    std::string selected;
    for (const auto& row : rows)
    {
        if (!row.contains("file") || !row.contains("output"))
        {
            continue;
        }
        const auto filename = row.at("file").get<std::string>();
        if (filename.ends_with("/Source/Runtime/Foundation/Private/Error.cpp") ||
            filename.ends_with("/Source/Runtime/Module/Private/ModuleIdentity.cpp"))
        {
            selected += row.at("output").get<std::string>() + "\n";
        }
    }
    if (std::ranges::count(selected, '\n') != 2)
    {
        std::cerr << "Expected two actual compiled Linux Runtime TUs\n";
        return 1;
    }
    const auto output = build / "Abi/Release/LinuxCollectorFixture";
    std::error_code error;
    std::filesystem::create_directories(output, error);
    if (error)
    {
        return 1;
    }
    const auto manifest = output / "objects.txt";
    if (!pg::abi::WriteFile(manifest, selected, report))
    {
        return 1;
    }
    pg::abi::CollectionRequest request;
    request.SourceRoot = root;
    request.BuildRoot = build;
    request.TargetName = "LinuxCollectorFixture";
    request.Configuration = "Release";
    request.HostPlatform = pg::abi::Platform::kLinux;
    request.TargetPlatform = pg::abi::Platform::kLinux;
    request.CommandLineStyle = pg::abi::CommandLineStyle::kPosix;
    request.Compiler = compiler;
    request.Database = build / "compile_commands.json";
    request.ObjectManifest = manifest;
    request.Library = build / "Lib/libPanguFoundation.a";
    request.OutputDirectory = output;
    request.SchemaFile = root / "wiki/foundation/abi-profile-v1.schema.json";
    request.SignatureHeader = output / "inputs.h";
    request.LinkGraph = output / "links.json";
    request.PlatformContext = nlohmann::json::object();
    const auto units = pg::abi::LoadCompileUnits(request, report);
    if (!units || units->size() != 2)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    // Known probe inputs and headers used only by the runtime witness must
    // be protected before any observation output is written. The aliases live
    // outside work; the target files inside work are ordinary single-link files.
    const auto probeText = pg::abi::ReadFile(root / "Tools/AbiProfile/Probe/Libcxx.cpp", "boundary_probe", report);
    const auto witnessText =
        pg::abi::ReadFile(root / "Tools/AbiProfile/Probe/PosixRuntimeWitness.cpp", "boundary_witness", report);
    if (!probeText || !witnessText)
    {
        return 1;
    }
    bool boundaryFailed = false;
    for (const bool witnessHeader : {false, true})
    {
        const auto fixture = output / "KnownProbeBoundary" / (witnessHeader ? "witness-header" : "probe");
        auto guarded = request;
        guarded.SourceRoot = fixture / "Sources";
        guarded.OutputDirectory = fixture / "Abi";
        guarded.SignatureHeader = guarded.OutputDirectory / "inputs.h";
        const auto probes = guarded.SourceRoot / "Tools/AbiProfile/Probe";
        const auto work = guarded.OutputDirectory / "work";
        std::filesystem::create_directories(probes, error);
        std::filesystem::create_directories(work, error);
        if (error)
        {
            return 1;
        }
        const auto protectedPath = work / (witnessHeader ? "runtime-witness" : "unit-0.cpp");
        const auto protectedText = witnessHeader ? std::string("int WitnessOnlyValue = 7;\n") : *probeText;
        if (!pg::abi::WriteFile(protectedPath, protectedText, report))
        {
            return 1;
        }
        const auto alias = probes / (witnessHeader ? "WitnessOnly.h" : "Libcxx.cpp");
        std::filesystem::remove(alias, error);
        std::filesystem::create_symlink(protectedPath, alias, error);
        if (error || (witnessHeader && !pg::abi::WriteFile(probes / "Libcxx.cpp", *probeText, report)) ||
            !pg::abi::WriteFile(probes / "PosixRuntimeWitness.cpp",
                                (witnessHeader ? "#include \"WitnessOnly.h\"\n" : "") + *witnessText, report))
        {
            return 1;
        }
        pg::abi::FailureReport boundaryReport;
        const auto seen = pg::abi::InspectLinuxCompileInputs(guarded, *units, boundaryReport);
        const auto after = pg::abi::ReadFile(protectedPath, "boundary_preserved", report);
        if (seen || !after || *after != protectedText)
        {
            std::cerr << "Observation wrote an aliased " << (witnessHeader ? "witness-only header" : "known probe")
                      << " before protecting its input bytes\n";
            boundaryFailed = true;
        }
    }
    // Runtime providers can be known only from the link trace. A private LLVM
    // prefix keeps this regression independent of the installed runtime files.
    const auto runtimeFixture = output / "KnownProbeBoundary/runtime-provider";
    auto runtimeRequest = request;
    runtimeRequest.OutputDirectory = runtimeFixture / "Abi";
    runtimeRequest.SignatureHeader = runtimeRequest.OutputDirectory / "inputs.h";
    const auto runtimeWork = runtimeRequest.OutputDirectory / "work";
    const auto installed = compiler.parent_path().parent_path();
    const auto privateLlvm = runtimeFixture / "LLVM";
    const auto privateRuntime = privateLlvm / "lib/x86_64-unknown-linux-gnu";
    std::filesystem::create_directories(runtimeWork, error);
    std::filesystem::create_directories(privateLlvm / "bin", error);
    std::filesystem::create_directories(privateRuntime, error);
    const auto provider = installed / "lib/x86_64-unknown-linux-gnu/libc++abi.so";
    const auto providerText = pg::abi::ReadFile(provider, "runtime_provider_bytes", report);
    const auto protectedProvider = runtimeWork / "unit-0.cpp";
    if (error || !providerText || !pg::abi::WriteFile(protectedProvider, *providerText, report))
    {
        return 1;
    }
    const auto link = [&](const auto& original, const auto& alias)
    {
        std::filesystem::remove(alias, error);
        std::filesystem::create_symlink(original, alias, error);
        return !error;
    };
    if (!link(compiler, privateLlvm / "bin/clang++") ||
        !link(compiler.parent_path() / "llvm-readobj", privateLlvm / "bin/llvm-readobj") ||
        !link(installed / "include", privateLlvm / "include"))
    {
        return 1;
    }
    for (const auto& entry : std::filesystem::directory_iterator(provider.parent_path()))
    {
        if (!link(entry.path().filename() == "libc++abi.so" ? protectedProvider : entry.path(),
                  privateRuntime / entry.path().filename()))
        {
            return 1;
        }
    }
    runtimeRequest.Compiler = privateLlvm / "bin/clang++";
    pg::abi::FailureReport runtimeReport;
    const auto runtimeSeen = pg::abi::InspectLinuxCompileInputs(runtimeRequest, *units, runtimeReport);
    const auto providerAfter = pg::abi::ReadFile(protectedProvider, "runtime_provider_preserved", report);
    if (runtimeSeen || !providerAfter || *providerAfter != *providerText)
    {
        std::cerr << "Observation overwrote an aliased runtime provider before final input protection\n";
        boundaryFailed = true;
    }
    if (boundaryFailed)
    {
        return 1;
    }
    const auto observation = pg::abi::InspectLinuxCompileInputs(request, *units, report);
    if (!observation)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    // Fresh private output directories must not alter reusable input evidence.
    const auto repeated = pg::abi::InspectLinuxCompileInputs(request, *units, report);
    if (!repeated || repeated->Facts != observation->Facts || repeated->Headers != observation->Headers ||
        repeated->ObservedInputs != observation->ObservedInputs)
    {
        std::cerr << "Private observation paths changed stable compile evidence\n";
        report.WriteJson(std::cerr);
        return 1;
    }
    const auto profile = pg::abi::CollectLinuxPlatformProfile(request, *units, *observation, report);
    if (!profile || profile->Profile.at("target").at("object") != "elf" ||
        profile->Profile.at("runtime").at("crt") != "glibc" ||
        profile->Profile.at("stdlib").at("abi_namespace") != "__1")
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    // REVIEW_REGRESSION_BEGIN: real -I headers must be content-snapshotted.
    // A source-local libc++ ABI switch and accepted ABI driver modes must never
    // inherit the default Profile merely because ordinary compile flags agree.
    bool reviewAccepted = false;
    const auto reviewSource = output / "ReviewAbi.cpp";
    const auto rejectsRealInputs =
        [&](std::string_view name, std::string_view sourceText, const std::vector<std::string>& flags)
    {
        if (!pg::abi::WriteFile(reviewSource, sourceText, report))
        {
            return false;
        }
        auto changed = *units;
        changed.at(1).Source = reviewSource;
        changed.at(1).AbiArguments.insert(changed.at(1).AbiArguments.end(), flags.begin(), flags.end());
        pg::abi::FailureReport failure;
        const auto seen = pg::abi::InspectLinuxCompileInputs(request, changed, failure);
        if (seen && pg::abi::CollectLinuxPlatformProfile(request, changed, *seen, failure))
        {
            std::cerr << "Accepted unsupported actual ABI input: " << name << '\n';
            reviewAccepted = true;
        }
        return true;
    };
    if (!rejectsRealInputs("source bounded vector iterators",
                           "#define _LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR\n#include <vector>\n", {}) ||
        !rejectsRealInputs("source restored after STL use",
                           "#define _LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR\n#include <vector>\n"
                           "#undef _LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR\n",
                           {}) ||
        !rejectsRealInputs("Microsoft bitfields", "struct Bits { unsigned char A:4; unsigned int B:4; };\n",
                           {"-mms-bitfields"}) ||
        !rejectsRealInputs("old Clang ABI compatibility", "#include <vector>\n", {"-fclang-abi-compat=4"}))
    {
        return 1;
    }
    if (!rejectsRealInputs("u8 character before restored ABI switch",
                           "constexpr auto kChar = u8'#';\n"
                           "#define _LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR\n#include <vector>\n"
                           "#undef _LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR\n",
                           {}))
    {
        return 1;
    }
    if (!rejectsRealInputs("u8 raw literal before restored ABI switch",
                           "constexpr auto kRaw = u8R\"tag(\")tag\";\n"
                           "#define _LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR\n#include <vector>\n"
                           "#undef _LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR\n",
                           {}))
    {
        return 1;
    }
    const auto overlay = output / "overlay";
    const auto selectedSite =
        compiler.parent_path().parent_path() / "include/x86_64-unknown-linux-gnu/c++/v1/__config_site";
    const auto forgedLineSource = "#line 1 \"" + selectedSite.generic_string() +
                                  "\"\n#define _LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR\n#include <vector>\n"
                                  "#undef _LIBCPP_ABI_BOUNDED_ITERATORS_IN_VECTOR\n";
    if (!rejectsRealInputs("forged presumed config-site origin", forgedLineSource, {}))
    {
        return 1;
    }
    const auto driverConfig = output / "abi-driver.cfg";
    if (!pg::abi::WriteFile(driverConfig, "-mms-bitfields\n", report) ||
        !rejectsRealInputs("driver config ABI injection", "#include <vector>\n",
                           {"--config=" + driverConfig.generic_string()}))
    {
        return 1;
    }
    std::filesystem::create_directories(overlay, error);
    if (error || !pg::abi::WriteFile(overlay / "version", "#include_next <version>\n", report) ||
        !pg::abi::WriteFile(reviewSource, "#include <version>\n", report))
    {
        return 1;
    }
    pg::abi::ProcessRequest overlayProbe;
    overlayProbe.Program = compiler;
    overlayProbe.Directory = units->front().Directory;
    overlayProbe.Arguments = units->front().AbiArguments;
    overlayProbe.Arguments.insert(overlayProbe.Arguments.end(),
                                  {"-I" + overlay.generic_string(), "-E", "-H", reviewSource.generic_string()});
    const auto overlayTrace = pg::abi::RunProcess(overlayProbe, report);
    const auto overlayHeaders =
        overlayTrace && overlayTrace->ExitCode == 0
            ? pg::abi::ParseClangHeaderTrace(overlayTrace->Stderr, overlayProbe.Directory, report)
            : pg::Result<std::vector<std::filesystem::path>>{};
    if (!overlayHeaders ||
        std::ranges::find(*overlayHeaders, std::filesystem::canonical(overlay / "version")) == overlayHeaders->end())
    {
        std::cerr << "Overlay characterization did not reach the real Clang header search\n";
        return 1;
    }
    if (!rejectsRealInputs("mixed version overlay", "#include <version>\n", {"-I" + overlay.generic_string()}))
    {
        return 1;
    }
    std::filesystem::remove(overlay / "version", error);
    if (error || !pg::abi::WriteFile(overlay / "payload.h", "#include_next <version>\n", report))
    {
        return 1;
    }
    std::filesystem::create_symlink("payload.h", overlay / "version", error);
    const auto aliasedTrace = pg::abi::RunProcess(overlayProbe, report);
    if (error || !aliasedTrace || aliasedTrace->ExitCode != 0 ||
        !aliasedTrace->Stderr.contains((overlay / "version").generic_string()) ||
        !rejectsRealInputs("aliased mixed version overlay", "#include <version>\n", {"-I" + overlay.generic_string()}))
    {
        std::cerr << "Aliased overlay characterization failed\n";
        return 1;
    }
    std::filesystem::remove(overlay / "version", error);
    if (error)
    {
        return 1;
    }
    constexpr std::string_view kOrdinarySource = R"source(
#define REVIEW_PROJECT_FEATURE 1
// #define _LIBCPP_ABI_COMMENTED_LINE
/* A multiline comment is whitespace.
#define _LIBCPP_ABI_COMMENTED_BLOCK
*/
constexpr char kOrdinary[] = "#define _LIBCPP_ABI_QUOTED";
constexpr char kContinued[] = "continued\
#define _LIBCPP_ABI_QUOTED_CONTINUATION";
constexpr char kRaw[] = R"literal(
#define _LIBCPP_ABI_RAW_STRING
)literal";
constexpr char kRawSplice[] = R"literal(
)lit\
eral"
#define _LIBCPP_ABI_RAW_STRING_SPLICE
)literal";
constexpr char8_t kUtf8Raw[] = u8R"literal("
#define _LIBCPP_ABI_UTF8_RAW_STRING
)literal";
constexpr auto kDigitSeparator = 1'024;
#include <vector>
)source";
    if (!pg::abi::WriteFile(reviewSource, kOrdinarySource, report))
    {
        return 1;
    }
    auto ordinary = *units;
    ordinary.at(1).Source = reviewSource;
    ordinary.at(1).AbiArguments.insert(ordinary.at(1).AbiArguments.end(),
                                       {"-DREVIEW_PROJECT_FLAG=1", "-g", "-fno-omit-frame-pointer"});
    const auto ordinarySeen = pg::abi::InspectLinuxCompileInputs(request, ordinary, report);
    const auto ordinaryProfile = ordinarySeen
                                     ? pg::abi::CollectLinuxPlatformProfile(request, ordinary, *ordinarySeen, report)
                                     : pg::Result<pg::abi::CollectedProfile>{};
    if (!ordinaryProfile || ordinaryProfile->Profile != profile->Profile)
    {
        std::cerr << "Ordinary project macros or debug flags were incorrectly rejected\n";
        report.WriteJson(std::cerr);
        return 1;
    }
    if (reviewAccepted)
    {
        return 1;
    }
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
    const auto aliasHeader = relativeDir / "AliasIncluded.h";
    if (!pg::abi::WriteFile(relativeDir / "AliasOne.h", "#pragma once\n", report) ||
        !pg::abi::WriteFile(relativeDir / "AliasTwo.h", "#pragma once\n", report) ||
        !pg::abi::WriteFile(probeSource, "#include <AliasIncluded.h>\n", report) ||
        !pg::abi::WriteFile(request.LinkGraph, "{}", report))
    {
        return 1;
    }
    std::filesystem::remove(aliasHeader, error);
    std::filesystem::create_symlink("AliasOne.h", aliasHeader, error);
    if (error)
    {
        return 1;
    }
    auto aliasUnits = *units;
    aliasUnits.at(1).Source = probeSource;
    aliasUnits.at(1).AbiArguments.emplace_back("-Ipangu-review-relative-headers");
    const auto aliasSeen = pg::abi::InspectLinuxCompileInputs(request, aliasUnits, report);
    if (!aliasSeen || std::ranges::find(aliasSeen->Headers, aliasHeader) == aliasSeen->Headers.end())
    {
        std::cerr << "Observed Header lost logical symlink spelling before snapshot\n";
        return 1;
    }
    const auto aliasBefore = pg::abi::SnapshotInputs(request, aliasUnits, *aliasSeen, report);
    const auto aliasIdentity = std::filesystem::canonical(aliasHeader).generic_string();
    if (!aliasBefore ||
        std::ranges::none_of(
            aliasBefore->Files, [&](const auto& file)
            { return file.at("path") == aliasHeader.generic_string() && file.at("identity") == aliasIdentity; }))
    {
        std::cerr << "Snapshot lost logical Header path or physical identity\n";
        report.WriteJson(std::cerr);
        return 1;
    }
    std::filesystem::remove(aliasHeader, error);
    std::filesystem::create_symlink("AliasTwo.h", aliasHeader, error);
    const auto aliasAfter = pg::abi::SnapshotInputs(request, aliasUnits, *aliasSeen, report);
    if (error || !aliasAfter || aliasAfter->CompileDigest == aliasBefore->CompileDigest)
    {
        std::cerr << "Retargeted Header symlink with identical bytes did not change compile inputs\n";
        report.WriteJson(std::cerr);
        return 1;
    }
    // REVIEW_REGRESSION_END.
    // REVIEW_REGRESSION_BEGIN: unsigned-char macro must not pass as signed.
    auto unsignedFacts = *observation;
    auto& unsignedMacroText = unsignedFacts.Facts.at("units").at(1).at("macros");
    unsignedMacroText = unsignedMacroText.get<std::string>() + "\n#define __CHAR_UNSIGNED__ 1\n";
    pg::abi::FailureReport unsignedReport;
    if (pg::abi::CollectLinuxPlatformProfile(request, *units, unsignedFacts, unsignedReport))
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
    const auto spoofObservation = pg::abi::InspectLinuxCompileInputs(request, compilerMacroSpoof, spoofReport);
    if (spoofObservation)
    {
        pg::abi::FailureReport mappingReport;
        if (pg::abi::CollectLinuxPlatformProfile(request, compilerMacroSpoof, *spoofObservation, mappingReport))
        {
            std::cerr << "Collector accepted char semantics with spoofed built-in macro\n";
            return 1;
        }
    }
    const auto rejectsEvidence = [&](std::string_view reason, const auto& mutate)
    {
        auto altered = *observation;
        mutate(altered.Facts);
        pg::abi::FailureReport invalid;
        if (pg::abi::CollectLinuxPlatformProfile(request, *units, altered, invalid))
        {
            std::cerr << "Linux Collector accepted forged provenance: " << reason << '\n';
            return false;
        }
        return true;
    };
    const auto replaceUnitMacro = [](auto& facts, std::string_view previous, std::string_view altered)
    {
        auto macros = facts.at("units").at(1).at("macros").template get<std::string>();
        const auto found = macros.find(previous);
        if (found != std::string::npos)
        {
            macros.replace(found, previous.size(), altered);
        }
        facts.at("units").at(1).at("macros") = std::move(macros);
    };
    if (!rejectsEvidence("distro version", [](auto& facts) { facts.at("sdk_id") = "linux-sysroot-ubuntu-22.04"; }) ||
        !rejectsEvidence("runtime providers",
                         [](auto& facts) { facts.at("runtime_libraries") = nlohmann::json::array(); }) ||
        !rejectsEvidence("ELF NEEDED dependencies",
                         [](auto& facts) { facts.at("runtime_needed_libraries") = "libc.so.6"; }) ||
        !rejectsEvidence(
            "libc++ implementation version", [&](auto& facts)
            { replaceUnitMacro(facts, "#define _LIBCPP_VERSION 230100", "#define _LIBCPP_VERSION 220100"); }) ||
        !rejectsEvidence(
            "libc++ ABI revision", [&](auto& facts)
            { replaceUnitMacro(facts, "#define _LIBCPP_ABI_VERSION 1", "#define _LIBCPP_ABI_VERSION 2"); }) ||
        !rejectsEvidence("libc++ hardening/layout mode",
                         [&](auto& facts)
                         {
                             replaceUnitMacro(facts, "#define _LIBCPP_HARDENING_MODE_DEFAULT 2",
                                              "#define _LIBCPP_HARDENING_MODE_DEFAULT 8");
                         }) ||
        !rejectsEvidence("target glibc metadata", [&](auto& facts)
                         { replaceUnitMacro(facts, "#define __GLIBC_MINOR__ 39", "#define __GLIBC_MINOR__ 36"); }) ||
        !rejectsEvidence("libc++ ABI namespace",
                         [](auto& facts)
                         {
                             auto text = facts.at("units").at(1).at("macros").template get<std::string>();
                             const auto marker = text.find("#define _LIBCPP_ABI_NAMESPACE __1");
                             if (marker != std::string::npos)
                             {
                                 text.replace(marker, std::string("#define _LIBCPP_ABI_NAMESPACE __1").size(),
                                              "#define _LIBCPP_ABI_NAMESPACE __2");
                             }
                             facts.at("units").at(1).at("macros") = text;
                         }))
    {
        return 1;
    }
    // The generated preflight wrapper must never overwrite a hard-linked input.
    const auto protectedInput = output / "linux-protected-input.txt";
    const auto wrapper = output / "work/unit-0.cpp";
    if (!pg::abi::WriteFile(protectedInput, "preserve original input", report))
    {
        return 1;
    }
    std::error_code hardlinkError;
    std::filesystem::remove(wrapper, hardlinkError);
    if (hardlinkError)
    {
        return 1;
    }
    std::filesystem::create_hard_link(protectedInput, wrapper, hardlinkError);
    if (!hardlinkError)
    {
        pg::abi::FailureReport linkReport;
        const auto linkedInspection = pg::abi::InspectLinuxCompileInputs(request, *units, linkReport);
        const auto original = pg::abi::ReadFile(protectedInput, "check_protected_input", report);
        std::filesystem::remove(wrapper);
        if (linkedInspection || !original || *original != "preserve original input")
        {
            std::cerr << "Linux Collector overwrote a hard-linked compiler input\n";
            return 1;
        }
    }
    else if (hardlinkError != std::errc::operation_not_supported && hardlinkError != std::errc::not_supported)
    {
        std::cerr << "Unexpected hardlink test failure: " << hardlinkError.message() << '\n';
        return 1;
    }
    auto fakeSysroot = *units;
    fakeSysroot.at(1).AbiArguments.emplace_back("--sysroot=/tmp/unverified");
    pg::abi::FailureReport badSysroot;
    if (pg::abi::InspectLinuxCompileInputs(request, fakeSysroot, badSysroot))
    {
        std::cerr << "Linux Collector accepted unknown cross sysroot\n";
        return 1;
    }
    return 0;
#endif
}
