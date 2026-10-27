#include "../../BuildTools/AbiProfile/Private/CollectionRequest.h"
#include "../../BuildTools/AbiProfile/Private/CollectionState.h"
#include "../../BuildTools/AbiProfile/Private/CompileCommands.h"
#include "../../BuildTools/AbiProfile/Private/Process.h"
#include "../../BuildTools/AbiProfile/Private/ToolIo.h"
#include "../../BuildTools/AbiProfile/Private/ToolReport.h"
#include "../../BuildTools/AbiProfile/Private/WindowsCollector.h"

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
#ifdef _WIN32
    // A real child process models a compiler whose first query succeeds and
    // whose resource query fails. Keep this fixture out of production seams.
    if (argc == 2)
    {
        const std::string_view query(std::span(argv, static_cast<std::size_t>(argc)).back());
        if (query == "--version")
        {
            std::cout << "clang version 23.1.0\n";
            return 0;
        }
        if (query == "--print-resource-dir")
        {
            pg::abi::FailureReport markerReport;
            if (!pg::abi::WriteFile(std::filesystem::current_path() / "resource-query-failed.marker",
                                    "resource query reached", markerReport))
            {
                return 18;
            }
            std::cerr << "resource query deliberately failed\n";
            return 17;
        }
    }
#endif
    if (argc != 5)
    {
        return 2;
    }
#ifdef _WIN32
    const auto manifestMatch =
        pg::abi::WindowsSdkManifestMatches(R"(<ApplicationPlatform version="10.0.26100.0" />)", "10.0.26100.0");
    const auto manifestWrongTag = pg::abi::WindowsSdkManifestMatches(
        R"(<Other version="10.0.26100.0"/><ApplicationPlatform version="10.0.19041.0"/>)", "10.0.26100.0");
    const auto manifestDuplicate = pg::abi::WindowsSdkManifestMatches(
        R"(<ApplicationPlatform version="10.0.26100.0"/><ApplicationPlatform version="10.0.19041.0"/>)",
        "10.0.26100.0");
    const auto manifestComment = pg::abi::WindowsSdkManifestMatches(
        R"(<!-- <ApplicationPlatform version="10.0.26100.0"/> --><Other version="10.0.19041.0"/>)", "10.0.26100.0");
    const auto manifestNested = pg::abi::WindowsSdkManifestMatches(
        R"(<Other><ApplicationPlatform version="10.0.26100.0"/></Other>)", "10.0.26100.0");
    const auto manifestPartialAttribute =
        pg::abi::WindowsSdkManifestMatches(R"(<ApplicationPlatform notversion="10.0.26100.0"/>)", "10.0.26100.0");
    if (!manifestMatch || manifestWrongTag || manifestDuplicate || manifestComment || manifestNested ||
        manifestPartialAttribute)
    {
        std::cerr << "SDK manifest must attest exactly one matching ApplicationPlatform root element\n";
        return 1;
    }
    const auto args = std::span(argv, static_cast<std::size_t>(argc)).subspan<1>();
    const std::filesystem::path sourceRoot(args.front());
    const std::filesystem::path buildRoot(args.subspan<1>().front());
    pg::abi::CollectionRequest request;
    request.SourceRoot = sourceRoot;
    request.BuildRoot = buildRoot;
    request.TargetName = "PanguAbiCollectionFixture";
    request.Configuration = "Release";
    request.HostPlatform = pg::abi::Platform::kWindows;
    request.TargetPlatform = pg::abi::Platform::kWindows;
    request.CommandLineStyle = pg::abi::CommandLineStyle::kWindows;
    request.Compiler = args.subspan<2>().front();
    request.Database = buildRoot / "compile_commands.json";
    request.ObjectManifest = buildRoot / "Tests/AbiProfile/WindowsFixture.objects";
    request.Library = buildRoot / "Lib/PanguAbiCollectionFixture.lib";
    request.OutputDirectory = buildRoot / "Abi/Release/PanguAbiCollectionFixture";
    request.SignatureHeader = request.OutputDirectory / "inputs.h";
    request.SchemaFile = sourceRoot / "wiki/foundation/abi-profile-v1.schema.json";
    request.LinkGraph = buildRoot / "Tests/AbiProfile/fixture-links.json";
    request.PlatformContext = nlohmann::json::object();
    pg::abi::FailureReport report;
    const auto units = pg::abi::LoadCompileUnits(request, report);
    if (!units || units->size() != 2)
    {
        std::cerr << "Windows Collector fixture requires both real objects\n";
        return 1;
    }
    // Apply the legacy missing/duplicate/response regressions to the actual
    // C++ database parser used by the new Collector, not the old CMake script.
    const auto cases = std::filesystem::path(args.subspan<3>().front()) / "WindowsCollectorDbCases";
    std::error_code directoryError;
    std::filesystem::create_directories(cases, directoryError);
    if (directoryError)
    {
        std::cerr << "Cannot prepare Windows Collector database regression fixtures\n";
        return 1;
    }
    auto entries = nlohmann::json::array();
    for (const auto& unit : *units)
    {
        entries.push_back({
            {"directory", unit.Directory.generic_string()},
            {"file", unit.Source.generic_string()},
            {"output", unit.Object.generic_string()},
            {"arguments", unit.OriginalArguments},
        });
    }
    const auto loadCase = [&](const nlohmann::json& database, std::string_view suffix)
    {
        auto adjusted = request;
        adjusted.Database = cases / (std::string(suffix) + ".json");
        pg::abi::FailureReport writeReport;
        if (!pg::abi::WriteFile(adjusted.Database, database.dump(), writeReport))
        {
            return pg::Result<std::vector<pg::abi::CompileUnit>>{std::unexpected(writeReport.Entries.at(0).Value)};
        }
        pg::abi::FailureReport loadReport;
        auto result = pg::abi::LoadCompileUnits(adjusted, loadReport);
        if (!result && suffix == "response-valid")
        {
            loadReport.WriteJson(std::cerr);
        }
        return result;
    };
    auto missing = entries;
    missing.erase(missing.begin() + 1);
    auto duplicate = entries;
    duplicate.push_back(entries.at(1));
    auto responseMissing = entries;
    responseMissing.at(1).at("arguments").push_back("@missing-collector-args.rsp");
    if (loadCase(missing, "missing") || loadCase(duplicate, "duplicate") ||
        loadCase(responseMissing, "response-missing"))
    {
        std::cerr << "Windows C++ Collector accepted missing, duplicate, or invalid response compiler entry\n";
        return 1;
    }
    const auto innerResponse = cases / "inner.rsp";
    const auto outerResponse = cases / "outer.rsp";
    if (!pg::abi::WriteFile(innerResponse, "-Wno-unused-variable\n", report) ||
        !pg::abi::WriteFile(outerResponse, "@\"" + innerResponse.generic_string() + "\"\n", report))
    {
        return 1;
    }
    auto responseValid = entries;
    responseValid.at(1).at("arguments").push_back("@" + outerResponse.generic_string());
    const auto expanded = loadCase(responseValid, "response-valid");
    if (!expanded || expanded->size() != 2)
    {
        std::cerr << "Windows C++ Collector failed real nested response-file expansion\n";
        return 1;
    }
    const auto tracksResponse = [&](const std::filesystem::path& expected)
    { return std::ranges::find(expanded->at(1).ResponseFiles, expected) != expanded->at(1).ResponseFiles.end(); };
    if (!tracksResponse(innerResponse) || !tracksResponse(outerResponse))
    {
        std::cerr << "Nested response file identities were not tracked\n";
        return 1;
    }
    // An extra compilation of the SAME source under another target must not
    // pollute the requested two object identities.
    auto unrelated = entries;
    unrelated.push_back({
        {"directory", units->front().Directory.generic_string()},
        {"file", units->front().Source.generic_string()},
        {"output", (cases / "OtherTarget.obj").generic_string()},
        {"arguments", units->front().OriginalArguments},
    });
    const auto exact = loadCase(unrelated, "other-target");
    if (!exact || exact->size() != 2)
    {
        std::cerr << "Windows C++ Collector confused shared source with target object identity\n";
        return 1;
    }
    const auto queryFixture = cases / "resource-query-failure";
    std::filesystem::create_directories(queryFixture, directoryError);
    auto failedQueryRequest = request;
    failedQueryRequest.Compiler = queryFixture / "clang++.exe";
    failedQueryRequest.BuildRoot = queryFixture;
    const auto queryMarker = queryFixture / "resource-query-failed.marker";
    std::filesystem::remove(queryMarker, directoryError);
    std::filesystem::copy_file(std::filesystem::absolute(std::span(argv, static_cast<std::size_t>(argc)).front()),
                               failedQueryRequest.Compiler, std::filesystem::copy_options::overwrite_existing,
                               directoryError);
    if (directoryError || !pg::abi::WriteFile(queryFixture / "llvm-readobj.exe", "fixture", report) ||
        !pg::abi::WriteFile(queryFixture / "lld-link.exe", "fixture", report))
    {
        return 1;
    }
    pg::abi::FailureReport failedQueryReport;
    const auto failedQuery = pg::abi::InspectWindowsCompileInputs(failedQueryRequest, *units, failedQueryReport);
    if (!std::filesystem::is_regular_file(queryMarker) || failedQuery || failedQueryReport.Count == 0 ||
        failedQuery.error() != failedQueryReport.Entries.at(failedQueryReport.Count - 1).Value)
    {
        std::cerr << "Failed resource query must propagate its own failure after a successful banner query\n";
        return 1;
    }
    // The compiler must observe the real top-level TU and the entire lifetime
    // of reserved ABI controls, including changes restored before EOF.
    for (const auto& [name, text] : std::vector<std::pair<std::string, std::string>>{
             {
                 "IteratorRestore",
                 "#undef _ITERATOR_DEBUG_LEVEL\n#define _ITERATOR_DEBUG_LEVEL 1\n"
                 "#include <vector>\nstd::vector<int> Values;\n"
                 "#undef _ITERATOR_DEBUG_LEVEL\n#define _ITERATOR_DEBUG_LEVEL 0\n",
             },
             {
                 "TopLevelControl",
                 "#if __INCLUDE_LEVEL__ == 0\n#define _HAS_EXCEPTIONS 0\n#endif\n"
                 "#include <vector>\nstd::vector<int> Values;\n",
             },
             {"ForcedHeaderRestore", "#include \"AbiControl.h\"\n#include <vector>\n"},
         })
    {
        auto changed = std::vector<pg::abi::CompileUnit>{units->front()};
        changed.front().Source = cases / (name + ".cpp");
        if (!pg::abi::WriteFile(changed.front().Source, text, report) ||
            !pg::abi::WriteFile(cases / "AbiControl.h",
                                "#define _HAS_STATIC_RTTI 0\n#undef _HAS_STATIC_RTTI\n"
                                "#define _HAS_STATIC_RTTI 1\n",
                                report))
        {
            return 1;
        }
        pg::abi::FailureReport controlReport;
        if (pg::abi::InspectWindowsCompileInputs(request, changed, controlReport))
        {
            std::cerr << "Windows Collector accepted source ABI lifecycle change: " << name << '\n';
            return 1;
        }
    }
    auto semanticUnit = units->front();
    semanticUnit.Source = cases / "TopLevelSemantics.cpp";
    const auto semanticText = "#include <string_view>\n#include \"RelativeSemantics.h\"\n"
                              "static_assert(__INCLUDE_LEVEL__ == 0);\n"
                              "static_assert(std::string_view(__FILE__) == \"" +
                              semanticUnit.Source.generic_string() +
                              "\");\nstatic_assert(std::string_view(__BASE_FILE__) == std::string_view(__FILE__));\n"
                              "#define APPLICATION_SETTING 7\nint ApplicationSetting = APPLICATION_SETTING;\n";
    if (!pg::abi::WriteFile(semanticUnit.Source, semanticText, report) ||
        !pg::abi::WriteFile(cases / "RelativeSemantics.h", "static_assert(__INCLUDE_LEVEL__ == 1);\n", report))
    {
        return 1;
    }
    pg::abi::FailureReport semanticReport;
    if (!pg::abi::InspectWindowsCompileInputs(request, std::span(&semanticUnit, 1), semanticReport))
    {
        std::cerr << "Observation changed top-level source or relative include semantics\n";
        semanticReport.WriteJson(std::cerr);
        return 1;
    }
    const auto observation = pg::abi::InspectWindowsCompileInputs(request, *units, report);
    if (!observation)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    // Fresh private output directories must not alter reusable input evidence.
    const auto repeated = pg::abi::InspectWindowsCompileInputs(request, *units, report);
    if (!repeated || repeated->Facts != observation->Facts || repeated->Headers != observation->Headers ||
        repeated->ObservedInputs != observation->ObservedInputs)
    {
        std::cerr << "Private observation paths changed stable compile evidence\n";
        report.WriteJson(std::cerr);
        return 1;
    }
    const auto profile = pg::abi::CollectWindowsPlatformProfile(request, *units, *observation, report);
    if (!profile)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    if (profile->Profile.at("target").at("object") != "coff" || profile->Profile.at("stdlib").at("id") != "msvc-stl" ||
        !profile->Evidence.contains("units") || profile->Evidence.at("units").size() != 2)
    {
        std::cerr << "Windows Collector must observe both real translation units\n";
        return 1;
    }
    // Exercise the actual post-compile object check without receipt validation
    // masking a missing CRT directive check in the platform collector.
    auto conflictingUnit = units->front();
    conflictingUnit.Source = cases / "CrtDirectiveConflict.cpp";
    conflictingUnit.Object = cases / "CrtDirectiveConflict.obj";
    if (!pg::abi::WriteFile(conflictingUnit.Source,
                            "#if defined(_DLL)\n#pragma comment(lib, \"libcmt.lib\")\n"
                            "#else\n#pragma comment(lib, \"msvcrt.lib\")\n#endif\n"
                            "#include <expected>\nstd::expected<int, int> CrtDirectiveConflict() { return 7; }\n",
                            report))
    {
        return 1;
    }
    auto conflictingRequest = request;
    conflictingRequest.OutputDirectory = cases / "CrtDirectiveConflict";
    pg::abi::FailureReport conflictingReport;
    const auto conflictingUnits = std::span(&conflictingUnit, 1);
    const auto conflictingObservation =
        pg::abi::InspectWindowsCompileInputs(conflictingRequest, conflictingUnits, conflictingReport);
    pg::abi::ProcessRequest conflictingCompile;
    conflictingCompile.Program = request.Compiler;
    conflictingCompile.Directory = conflictingUnit.Directory;
    conflictingCompile.Arguments = conflictingUnit.AbiArguments;
    conflictingCompile.Arguments.insert(
        conflictingCompile.Arguments.end(),
        {"-c", conflictingUnit.Source.generic_string(), "-o", conflictingUnit.Object.generic_string()});
    const auto conflictingObject = pg::abi::RunProcess(conflictingCompile, conflictingReport);
    if (!conflictingObservation || !conflictingObject || conflictingObject->ExitCode != 0)
    {
        std::cerr << "Cannot prepare genuine CRT pragma object\n";
        conflictingReport.WriteJson(std::cerr);
        return 1;
    }
    const auto conflictingProfile = pg::abi::CollectWindowsPlatformProfile(conflictingRequest, conflictingUnits,
                                                                           *conflictingObservation, conflictingReport);
    if (conflictingProfile || conflictingReport.Count == 0 ||
        conflictingReport.Entries.at(conflictingReport.Count - 1).Stage.View() != "windows_object_runtime")
    {
        std::cerr << "Collector accepted actual CRT pragma conflicting with the observed CRT policy\n";
        return 1;
    }
    // Expanding nested response files may change warning behavior, but must
    // preserve the exact ABI Profile for every included translation unit.
    pg::abi::FailureReport responseReport;
    const auto expandedObservation = pg::abi::InspectWindowsCompileInputs(request, *expanded, responseReport);
    if (!expandedObservation)
    {
        responseReport.WriteJson(std::cerr);
        return 1;
    }
    const auto expandedProfile =
        pg::abi::CollectWindowsPlatformProfile(request, *expanded, *expandedObservation, responseReport);
    if (!expandedProfile || expandedProfile->Profile != profile->Profile)
    {
        std::cerr << "Expanded compiler response unexpectedly changed the Windows ABI Profile\n";
        responseReport.WriteJson(std::cerr);
        return 1;
    }
    // Fail closed if the SDK, actual STL header, or observed runtime provider
    // is altered before the Profile mapping stage.
    const auto rejectEvidence = [&](std::string_view reason, const auto& mutate)
    {
        auto altered = *observation;
        mutate(altered.Facts);
        pg::abi::FailureReport invalid;
        if (pg::abi::CollectWindowsPlatformProfile(request, *units, altered, invalid))
        {
            std::cerr << "Windows Collector accepted forged provenance: " << reason << '\n';
            return false;
        }
        return true;
    };
    if (!rejectEvidence("sdk manifest", [](auto& facts) { facts.at("sdk_version") = "0.0"; }) ||
        !rejectEvidence("STL origin", [](auto& facts) { facts.at("stl_header") = "C:/forged/yvals_core.h"; }) ||
        !rejectEvidence("runtime", [](auto& facts) { facts.at("runtime_libraries") = nlohmann::json::array(); }) ||
        !rejectEvidence("MSVC STL ABI switch",
                        [](auto& facts)
                        {
                            auto& macros = facts.at("units").at(1).at("macros");
                            auto text = macros.template get<std::string>();
                            const auto marker = std::string("#define _HAS_STATIC_RTTI 1");
                            const auto offset = text.find(marker);
                            if (offset != std::string::npos)
                            {
                                text.replace(offset, marker.size(), "#define _HAS_STATIC_RTTI 0");
                            }
                            macros = text;
                        }))
    {
        return 1;
    }
    // The selected second TU must not quietly change its runtime RTTI mode.
    auto mismatchedRttiUnits = *units;
    mismatchedRttiUnits.at(1).AbiArguments.emplace_back("-fno-rtti");
    pg::abi::FailureReport rttiReport;
    const auto rttiObservation = pg::abi::InspectWindowsCompileInputs(request, mismatchedRttiUnits, rttiReport);
    if (rttiObservation &&
        pg::abi::CollectWindowsPlatformProfile(request, mismatchedRttiUnits, *rttiObservation, rttiReport))
    {
        std::cerr << "Windows Collector accepted mixed RTTI modes across translation units\n";
        return 1;
    }

    // A generated probe source must not overwrite an existing hard-linked input.
    const auto protectedInput = request.OutputDirectory / "collector-protected-input.txt";
    const auto wrapper = request.OutputDirectory / "work/unit-0.cpp";
    std::error_code aliasError;
    if (!pg::abi::WriteFile(protectedInput, "preserve untrusted input", report))
    {
        return 1;
    }
    std::filesystem::remove(wrapper, aliasError);
    if (aliasError)
    {
        return 1;
    }
    std::filesystem::create_hard_link(protectedInput, wrapper, aliasError);
    if (!aliasError)
    {
        pg::abi::FailureReport aliasReport;
        const auto aliasInspection = pg::abi::InspectWindowsCompileInputs(request, *units, aliasReport);
        pg::abi::FailureReport readReport;
        const auto protectedText = pg::abi::ReadFile(protectedInput, "verify_protected", readReport);
        const bool rejectedAndProtected =
            !aliasInspection && protectedText && *protectedText == "preserve untrusted input";
        std::filesystem::remove(wrapper);
        std::filesystem::remove(protectedInput);
        if (!rejectedAndProtected)
        {
            std::cerr << "Windows Collector overwrote hard-linked probe input\n";
            return 1;
        }
    }
    else
    {
        std::cerr << "Hardlink creation unavailable: " << aliasError.message() << '\n';
    }

    // Compile-input inspection runs before the compiler has created the .obj.
    // Final profile collection, in contrast, must require real object bytes.
    auto coldUnits = *units;
    coldUnits.at(1).Object = request.OutputDirectory / "cold-not-yet-compiled.obj";
    std::filesystem::remove(coldUnits.at(1).Object);
    pg::abi::FailureReport coldReport;
    const auto coldObservation = pg::abi::InspectWindowsCompileInputs(request, coldUnits, coldReport);
    if (!coldObservation)
    {
        std::cerr << "Cold-build precompile inspection incorrectly requires object files\n";
        coldReport.WriteJson(std::cerr);
        return 1;
    }
    pg::abi::FailureReport coldCollectReport;
    if (pg::abi::CollectWindowsPlatformProfile(request, coldUnits, *coldObservation, coldCollectReport))
    {
        std::cerr << "Cold-build collector accepted missing real object bytes\n";
        return 1;
    }

    // An unknown Clang ABI rewriting option must not be silently accepted.
    auto badUnits = *units;
    badUnits.at(1).AbiArguments.emplace_back("-fpack-struct=1");
    pg::abi::FailureReport badOptions;
    if (pg::abi::InspectWindowsCompileInputs(request, badUnits, badOptions))
    {
        std::cerr << "Windows Collector accepted changed structure packing\n";
        return 1;
    }

    // A mapped Profile alone must not claim a build completion without compile receipts.
    if (std::filesystem::exists(request.OutputDirectory / "complete.json"))
    {
        std::cerr << "A standalone collection observation must not publish complete.json\n";
        return 1;
    }
#else
    (void)argv;
#endif
    return 0;
}
