#include "../../Tools/AbiProfile/Private/CollectionRequest.h"
#include "../../Tools/AbiProfile/Private/CollectionState.h"
#include "../../Tools/AbiProfile/Private/CompileCommands.h"
#include "../../Tools/AbiProfile/Private/Process.h"
#include "../../Tools/AbiProfile/Private/ToolIo.h"
#include "../../Tools/AbiProfile/Private/ToolReport.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <future>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
using pg::abi::CollectionRequest;
using pg::abi::FailureReport;
using pg::abi::ProcessRequest;

[[nodiscard]] bool Write(const std::filesystem::path& path, std::string_view contents)
{
    FailureReport report;
    if (pg::abi::WriteFile(path, contents, report))
    {
        return true;
    }
    report.WriteJson(std::cerr);
    return false;
}

[[nodiscard]] bool Run(const std::filesystem::path& program, std::vector<std::string> args,
                       const std::filesystem::path& directory)
{
    FailureReport report;
    ProcessRequest request;
    request.Program = program;
    request.Directory = directory;
    request.Arguments = std::move(args);
    // Debug CRT/STL probing across multiple real TUs may exceed the default
    // short process-probe timeout; the CTest itself remains bounded.
    request.Timeout = std::chrono::minutes(6);
    const auto result = pg::abi::RunProcess(request, report);
    if (!result || result->ExitCode != 0)
    {
        report.WriteJson(std::cerr);
        if (result)
        {
            std::cerr << "CLI exit=" << result->ExitCode << " stdout=" << result->Stdout << " stderr=" << result->Stderr
                      << '\n';
        }
        return false;
    }
    return true;
}

[[nodiscard]] bool Validate(const std::filesystem::path& path)
{
    FailureReport report;
    const auto text = pg::abi::ReadFile(path, "read_completed_profile", report);
    if (!text)
    {
        return false;
    }
    const auto json = nlohmann::json::parse(*text, nullptr, false);
    return !json.is_discarded() && json.is_object() && json.contains("fingerprint");
}
} // namespace

int main(int argc, char* const argv[])
{
#ifndef _WIN32
    (void)argc;
    (void)argv;
    return 0;
#else
    if (argc != 6)
    {
        return 2;
    }
    const auto args = std::span(argv, static_cast<std::size_t>(argc));
    const std::string configuration(args.subspan<5>().front());
    const auto sourceRoot = std::filesystem::absolute(args.subspan<1>().front());
    const auto buildRoot = std::filesystem::absolute(args.subspan<2>().front());
    const std::filesystem::path compiler(args.subspan<3>().front());
    const std::filesystem::path tool(args.subspan<4>().front());
    const auto current = std::filesystem::current_path();
    const auto work = buildRoot / "Tests/AbiProfile/WindowsCppCollectorCli";
    std::error_code error;
    std::filesystem::remove_all(work, error);
    if (error)
    {
        std::cerr << "Cannot reset private CLI fixture\n";
        return 1;
    }
    std::filesystem::create_directories(work, error);
    if (error)
    {
        return 1;
    }

    CollectionRequest baseline;
    baseline.SourceRoot = sourceRoot;
    baseline.BuildRoot = buildRoot;
    baseline.Compiler = compiler;
    baseline.Database = buildRoot / "compile_commands.json";
    baseline.ObjectManifest = buildRoot / "Tests/AbiProfile/WindowsFixture.objects";
    baseline.HostPlatform = pg::abi::Platform::kWindows;
    baseline.CommandLineStyle = pg::abi::CommandLineStyle::kWindows;
    FailureReport report;
    const auto original = pg::abi::LoadCompileUnits(baseline, report);
    if (!original || original->size() != 2)
    {
        report.WriteJson(std::cerr);
        return 1;
    }

    const auto output = buildRoot / "Abi" / configuration / "WindowsCppCollectorCli";
    // Reset only the private fixture's previously generated output; a second
    // test invocation must not mistake old compiler receipts for this run.
    std::filesystem::remove_all(output, error);
    if (error)
    {
        std::cerr << "Cannot reset private ABI test output\n";
        return 1;
    }
    const auto requestFile = work / "request.json";
    const auto manifest = work / "objects.txt";
    const auto database = work / "compile_commands.json";
    const auto graph = work / "links.json";
    const auto library = work / "libCollectorFixture.lib";
    auto commands = nlohmann::json::array();
    std::vector<std::filesystem::path> objects;
    std::vector<std::vector<std::string>> invocations;
    std::string manifestText;
    for (std::size_t index = 0; index < original->size(); ++index)
    {
        const auto object = work / ("unit-" + std::to_string(index) + ".obj");
        objects.push_back(object);
        manifestText += object.generic_string() + "\n";
        std::vector<std::string> command{compiler.generic_string()};
        command.insert(command.end(), original->at(index).AbiArguments.begin(), original->at(index).AbiArguments.end());
        command.insert(command.end(),
                       {"-c", original->at(index).Source.generic_string(), "-o", object.generic_string()});
        commands.push_back({
            {"file", original->at(index).Source.generic_string()},
            {"directory", current.generic_string()},
            {"output", object.generic_string()},
            {"arguments", command},
        });
        invocations.push_back(std::move(command));
    }
    // A STATIC library may legitimately defer an application-owned symbol to
    // the final EXE. Keep that object in the archive and the ABI collection.
    const auto deferredSource = work / "HostDeferred.cpp";
    const auto deferredObject = work / "HostDeferred.obj";
    if (!Write(deferredSource, "#include <expected>\n"
                               "extern \"C\" int HostOnly() noexcept;\n"
                               "std::expected<int, int> ModuleDeferredSymbol() noexcept { return HostOnly(); }\n"))
    {
        return 1;
    }
    std::vector<std::string> deferredCommand{compiler.generic_string()};
    deferredCommand.insert(deferredCommand.end(), original->front().AbiArguments.begin(),
                           original->front().AbiArguments.end());
    deferredCommand.insert(deferredCommand.end(),
                           {"-c", deferredSource.generic_string(), "-o", deferredObject.generic_string()});
    commands.push_back({
        {"file", deferredSource.generic_string()},
        {"directory", current.generic_string()},
        {"output", deferredObject.generic_string()},
        {"arguments", deferredCommand},
    });
    objects.push_back(deferredObject);
    manifestText += deferredObject.generic_string() + "\n";
    invocations.push_back(std::move(deferredCommand));

    const auto fourthSource = work / "FourthTranslationUnit.cpp";
    const auto fourthObject = work / "FourthTranslationUnit.obj";
    if (!Write(fourthSource, "#include <vector>\nstd::vector<int> FourthValues;\n"))
    {
        return 1;
    }
    std::vector<std::string> fourthCommand{compiler.generic_string()};
    fourthCommand.insert(fourthCommand.end(), original->front().AbiArguments.begin(),
                         original->front().AbiArguments.end());
    fourthCommand.insert(fourthCommand.end(),
                         {"-c", fourthSource.generic_string(), "-o", fourthObject.generic_string()});
    commands.push_back({
        {"file", fourthSource.generic_string()},
        {"directory", current.generic_string()},
        {"output", fourthObject.generic_string()},
        {"arguments", fourthCommand},
    });
    objects.push_back(fourthObject);
    manifestText += fourthObject.generic_string() + "\n";
    invocations.push_back(std::move(fourthCommand));

    const nlohmann::json dependencyGraph{
        {"schema", 1},
        {"format", "cmake-actual-v1"},
        {"configuration", configuration},
        {"aliases", nlohmann::json::array()},
        {
            "targets",
            nlohmann::json::array({
                {
                    {"name", "WindowsCppCollectorCli"},
                    {"kind", "module"},
                    {"language", "cxx"},
                    {"managed", true},
                    {"source_visible", true},
                    {"links", nlohmann::json::array()},
                },
            }),
        },
    };
    if (!Write(manifest, manifestText) || !Write(database, commands.dump()) || !Write(graph, dependencyGraph.dump()))
    {
        return 1;
    }
    const nlohmann::json request = {
        {"schema", 2},
        {"source_root", sourceRoot.generic_string()},
        {"build_root", buildRoot.generic_string()},
        {"target_name", "WindowsCppCollectorCli"},
        {"configuration", configuration},
        {"host_platform", "windows"},
        {"target_platform", "windows"},
        {"command_line_style", "windows"},
        {"compiler", compiler.generic_string()},
        {"database", database.generic_string()},
        {"object_manifest", manifest.generic_string()},
        {"library", library.generic_string()},
        {"output_directory", output.generic_string()},
        {"schema_file", (sourceRoot / "wiki/foundation/abi-profile-v1.schema.json").generic_string()},
        {"signature_header", (output / "inputs.h").generic_string()},
        {"link_graph", graph.generic_string()},
        {"platform_context", nlohmann::json::object()},
        {"environment_baseline", nlohmann::json::object()},
    };
    if (!Write(requestFile, request.dump()))
    {
        return 1;
    }
    // All object files are initially absent: the regular CLI must handle cold build.
    if (!Run(tool, {"guard", "--request", requestFile.generic_string()}, current))
    {
        return 1;
    }
    // Independent compiler wrappers share the target observation work area.
    // Every real compile and receipt must succeed under normal parallel builds.
    std::vector<std::future<bool>> parallelCompiles;
    parallelCompiles.reserve(invocations.size());
    for (const auto& invocation : invocations)
    {
        parallelCompiles.push_back(std::async(std::launch::async,
                                              [&, invocation]
                                              {
                                                  std::vector<std::string> command{
                                                      "compile",
                                                      "--request",
                                                      requestFile.generic_string(),
                                                      "--",
                                                  };
                                                  command.insert(command.end(), invocation.begin(), invocation.end());
                                                  return Run(tool, std::move(command), current);
                                              }));
    }
    for (auto& compiled : parallelCompiles)
    {
        if (!compiled.get())
        {
            std::cerr << "Concurrent compiler wrapper lost its observation or receipt\n";
            return 1;
        }
    }
    std::vector<std::string> arArgs{"/nologo", "/OUT:" + library.generic_string()};
    for (const auto& object : objects)
    {
        arArgs.emplace_back(object.generic_string());
    }
    if (!Run(compiler.parent_path() / "llvm-lib.exe", std::move(arArgs), current))
    {
        return 1;
    }
    // RED: a malformed dependency graph must not publish a successful Profile.
    if (!Write(graph, "{}") || Run(tool, {"collect", "--request", requestFile.generic_string()}, current) ||
        std::filesystem::exists(output / "complete.json") || !Write(graph, dependencyGraph.dump()))
    {
        std::cerr << "Invalid dependency graph escaped the Collector\n";
        return 1;
    }
    auto unattested = dependencyGraph;
    unattested.at("targets").push_back({
        {"name", "UnattestedTech"},
        {"kind", "static"},
        {"language", "cxx"},
        {"managed", false},
        {"source_visible", true},
        {"links", nlohmann::json::array()},
    });
    unattested.at("targets").at(0).at("links").push_back({{"target", "UnattestedTech"}, {"scope", "private"}});
    if (!Write(graph, unattested.dump()) ||
        Run(tool, {"collect", "--request", requestFile.generic_string()}, current) ||
        std::filesystem::exists(output / "complete.json") || !Write(graph, dependencyGraph.dump()))
    {
        std::cerr << "Collector published an unattested reachable STATIC dependency\n";
        return 1;
    }
    if (!Run(tool, {"collect", "--request", requestFile.generic_string()}, current) ||
        !Validate(output / "complete.json") ||
        !Run(tool, {"guard", "--request", requestFile.generic_string()}, current))
    {
        std::cerr << "Full Windows C++ CLI fixture did not complete\n";
        return 1;
    }
    // Cross-check the independently collected C++ Profile against the frozen
    // Profile produced by the real Module build for the SAME compiler/CRT config.
    // This preserves the v1 fingerprint protocol without the legacy scripts.
    const auto baselinePath = buildRoot / "Abi" / configuration / "PanguFoundation";
    const auto cppProfile = pg::abi::ReadFile(output / "profile.json", "cpp_profile", report);
    const auto legacyProfile = pg::abi::ReadFile(baselinePath / "profile.json", "legacy_profile", report);
    const auto cppFingerprint = pg::abi::ReadFile(output / "fingerprint.sha256", "cpp_fingerprint", report);
    const auto legacyFingerprint = pg::abi::ReadFile(baselinePath / "fingerprint.sha256", "legacy_fingerprint", report);
    if (!cppProfile || !legacyProfile || !cppFingerprint || !legacyFingerprint ||
        nlohmann::json::parse(*cppProfile, nullptr, false) != nlohmann::json::parse(*legacyProfile, nullptr, false) ||
        *cppFingerprint != *legacyFingerprint)
    {
        std::cerr << "C++ Collector disagrees with the frozen Windows ABI Profile"
                  << " for configuration " << configuration << '\n';
        return 1;
    }

    const auto completed = pg::abi::ReadFile(output / "complete.json", "completed", report);
    if (!completed || nlohmann::json::parse(*completed).at("receipts").size() != 4)
    {
        std::cerr << "Completeness must bind all four genuine compiler receipts\n";
        return 1;
    }
    // The collection accepted a genuine STATIC archive containing an unresolved
    // application-owned symbol. Verify the final consumer can resolve it, but
    // never execute the linked EXE as part of this ABI collection test.
    ProcessRequest symbols;
    symbols.Program = compiler.parent_path() / "llvm-nm.exe";
    symbols.Directory = current;
    symbols.Arguments = {"--undefined-only", deferredObject.generic_string()};
    const auto undefined = pg::abi::RunProcess(symbols, report);
    if (!undefined || undefined->ExitCode != 0 || !undefined->Stdout.contains("HostOnly"))
    {
        std::cerr << "Expected the deferred host symbol to remain unresolved in the archive\n";
        return 1;
    }
    const auto consumerSource = work / "FinalConsumer.cpp";
    const auto consumerObject = work / "FinalConsumer.obj";
    const auto consumerExe = work / "FinalConsumer.exe";
    if (!Write(consumerSource,
               "#include <expected>\n"
               "extern std::expected<int, int> ModuleDeferredSymbol() noexcept;\n"
               "extern \"C\" int HostOnly() noexcept { return 7; }\n"
               "int main() { auto result = ModuleDeferredSymbol(); return result ? *result - 7 : 1; }\n"))
    {
        return 1;
    }
    std::vector<std::string> consumerArgs = original->front().AbiArguments;
    consumerArgs.insert(consumerArgs.end(),
                        {"-c", consumerSource.generic_string(), "-o", consumerObject.generic_string()});
    if (!Run(compiler, std::move(consumerArgs), current) ||
        !Run(compiler,
             {
                 "-fuse-ld=lld",
                 consumerObject.generic_string(),
                 library.generic_string(),
                 "-o",
                 consumerExe.generic_string(),
                 "-lkernel32",
             },
             current) ||
        !std::filesystem::is_regular_file(consumerExe))
    {
        std::cerr << "Valid final consumer failed to resolve deferred host symbol\n";
        return 1;
    }
    // A real source pragma must be checked against the actual COFF directives,
    // even though the independent runtime witness still uses the default CRT.
    if (!Write(deferredSource, "#if defined(_DLL)\n#pragma comment(lib, \"libcmt.lib\")\n"
                               "#else\n#pragma comment(lib, \"msvcrt.lib\")\n#endif\n"
                               "#include <expected>\n"
                               "extern \"C\" int HostOnly() noexcept;\n"
                               "std::expected<int, int> ModuleDeferredSymbol() noexcept { return HostOnly(); }\n"))
    {
        return 1;
    }
    // Source content participates in the target snapshot: refresh every receipt
    // so an unrelated stale-receipt rejection cannot mask the CRT regression.
    for (const auto& invocation : invocations)
    {
        std::vector<std::string> conflictingCommand{"compile", "--request", requestFile.generic_string(), "--"};
        conflictingCommand.insert(conflictingCommand.end(), invocation.begin(), invocation.end());
        if (!Run(tool, std::move(conflictingCommand), current))
        {
            std::cerr << "Cannot compile real CRT pragma regression\n";
            return 1;
        }
    }
    std::vector<std::string> conflictingArchive{"/nologo", "/OUT:" + library.generic_string()};
    for (const auto& object : objects)
    {
        conflictingArchive.push_back(object.generic_string());
    }
    if (!Run(compiler.parent_path() / "llvm-lib.exe", std::move(conflictingArchive), current))
    {
        std::cerr << "Cannot compile and archive real CRT pragma regression\n";
        return 1;
    }
    ProcessRequest conflictingCollect;
    conflictingCollect.Program = tool;
    conflictingCollect.Directory = current;
    conflictingCollect.Arguments = {"collect", "--request", requestFile.generic_string()};
    conflictingCollect.Timeout = std::chrono::minutes(6);
    FailureReport conflictingReport;
    const auto conflictingResult = pg::abi::RunProcess(conflictingCollect, conflictingReport);
    const auto conflictingJson =
        conflictingResult ? nlohmann::json::parse(conflictingResult->Stdout, nullptr, false) : nlohmann::json{};
    bool rejectedCrt = false;
    if (!conflictingJson.is_discarded() && conflictingJson.is_object() && conflictingJson.contains("errors"))
    {
        for (const auto& entry : conflictingJson.at("errors"))
        {
            rejectedCrt |= entry.value("stage", "") == "windows_object_runtime";
        }
    }
    if (!conflictingResult || conflictingResult->ExitCode == 0 || !rejectedCrt ||
        std::filesystem::exists(output / "complete.json"))
    {
        std::cerr << "Collector accepted actual COFF objects with conflicting MD/MT CRT requests\n";
        if (conflictingResult)
        {
            std::cerr << conflictingResult->Stdout << conflictingResult->Stderr;
        }
        return 1;
    }
    // A later collection with a missing compiler receipt must revoke completion.
    const auto selected = pg::abi::LoadCompileUnits(*pg::abi::ReadCollectionRequest(requestFile, report), report);
    if (!selected || selected->empty())
    {
        return 1;
    }
    std::filesystem::remove(
        pg::abi::CompileReceiptPath(*pg::abi::ReadCollectionRequest(requestFile, report), selected->front()));
    if (Run(tool, {"collect", "--request", requestFile.generic_string()}, current) ||
        std::filesystem::exists(output / "complete.json"))
    {
        std::cerr << "Missing compiler proof allowed a completed Profile\n";
        return 1;
    }
    return 0;
#endif
}
