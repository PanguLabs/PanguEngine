#include "../../BuildTools/AbiProfile/Private/CollectionRequest.h"
#include "../../BuildTools/AbiProfile/Private/CollectionState.h"
#include "../../BuildTools/AbiProfile/Private/CompileCommands.h"
#include "../../BuildTools/AbiProfile/Private/Process.h"
#include "../../BuildTools/AbiProfile/Private/ToolIo.h"
#include "../../BuildTools/AbiProfile/Private/ToolReport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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
[[nodiscard]] bool Write(const std::filesystem::path& path, std::string_view contents)
{
    pg::abi::FailureReport report;
    if (pg::abi::WriteFile(path, contents, report))
    {
        return true;
    }
    report.WriteJson(std::cerr);
    return false;
}

[[nodiscard]] int Run(const std::filesystem::path& executable, const std::filesystem::path& working,
                      std::vector<std::string> arguments)
{
    pg::abi::FailureReport report;
    pg::abi::ProcessRequest request;
    request.Program = executable;
    request.Directory = working;
    request.Arguments = std::move(arguments);
    const auto result = pg::abi::RunProcess(request, report);
    if (!result)
    {
        report.WriteJson(std::cerr);
        return -1;
    }
    if (result->ExitCode != 0)
    {
        std::cerr << executable.generic_string() << " returned " << result->ExitCode << "\nstdout: " << result->Stdout
                  << "\nstderr: " << result->Stderr << '\n';
    }
    return result->ExitCode;
}

[[nodiscard]] bool Completed(const std::filesystem::path& output, std::size_t expectedReceipts,
                             std::string_view platform)
{
    pg::abi::FailureReport report;
    const auto complete = pg::abi::ReadFile(output / "complete.json", "read_complete", report);
    const auto fingerprint = pg::abi::ReadFile(output / "fingerprint.sha256", "read_digest", report);
    const auto profile = pg::abi::ReadFile(output / "profile.json", "read_profile", report);
    const auto canonical = pg::abi::ReadFile(output / "canonical.json", "read_canonical", report);
    if (!complete || !fingerprint || !profile || !canonical)
    {
        report.WriteJson(std::cerr);
        return false;
    }
    const auto state = nlohmann::json::parse(*complete, nullptr, false);
    const auto parsed = nlohmann::json::parse(*profile, nullptr, false);
    const bool sha = fingerprint->size() == 64 &&
                     std::ranges::all_of(*fingerprint, [](unsigned char ch)
                                         { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
    return !state.is_discarded() && !parsed.is_discarded() && state.contains("fingerprint") &&
           state.at("fingerprint") == *fingerprint && state.contains("receipts") &&
           state.at("receipts").size() == expectedReceipts && sha && parsed.at("target").at("object") == "macho" &&
           parsed.at("stdlib").at("abi_namespace") == "__1" &&
           parsed.at("runtime").at("sdk_abi_id") == (platform == "ios" ? "ios-sdk-18.2" : "macos-sdk-15.2");
}
} // namespace

int main(int argc, char* const argv[])
{
#ifndef __APPLE__
    (void)argc;
    (void)argv;
    return 0;
#else
    if (argc != 6)
    {
        return 2;
    }
    const auto args = std::span(argv, static_cast<std::size_t>(argc));
    const auto root = std::filesystem::absolute(args.subspan<1>().front());
    const auto build = std::filesystem::absolute(args.subspan<2>().front());
    const auto compiler = std::filesystem::path(args.subspan<3>().front());
    const auto tool = std::filesystem::path(args.subspan<4>().front());
    const auto platform = std::string(args.subspan<5>().front());
    if (platform != "macos" && platform != "ios")
    {
        return 2;
    }
    const auto current = std::filesystem::current_path();
    const auto work = build / "Tests/AbiProfile/AppleCppCollectorCli";
    const auto output = build / "Abi/Release/AppleCppCollectorCli";
    std::error_code error;
    // Only test-owned directories are reset. Production target build results are untouched.
    std::filesystem::remove_all(work, error);
    if (error)
    {
        return 1;
    }
    std::filesystem::remove_all(output, error);
    if (error)
    {
        return 1;
    }
    std::filesystem::create_directories(work, error);
    if (error)
    {
        return 1;
    }
    pg::abi::FailureReport report;
    const auto file = pg::abi::ReadFile(build / "compile_commands.json", "apple_cli_database", report);
    if (!file)
    {
        return 1;
    }
    const auto database = nlohmann::json::parse(*file, nullptr, false);
    if (!database.is_array())
    {
        return 1;
    }
    std::string targetObjects;
    for (const auto& entry : database)
    {
        if (!entry.is_object() || !entry.contains("file") || !entry.contains("output"))
        {
            continue;
        }
        const auto name = entry.at("file").get<std::string>();
        if (name.ends_with("/Source/Runtime/Foundation/Private/Error.cpp") ||
            name.ends_with("/Source/Runtime/Module/Private/ModuleIdentity.cpp"))
        {
            targetObjects += entry.at("output").get<std::string>() + "\n";
        }
    }
    const auto manifest = work / "selected.txt";
    if (std::ranges::count(targetObjects, '\n') != 2 || !Write(manifest, targetObjects))
    {
        std::cerr << "Could not isolate two real Apple Runtime compilation commands\n";
        return 1;
    }
    pg::abi::CollectionRequest selectedRequest;
    selectedRequest.SourceRoot = root;
    selectedRequest.BuildRoot = build;
    selectedRequest.Compiler = compiler;
    selectedRequest.Database = build / "compile_commands.json";
    selectedRequest.ObjectManifest = manifest;
    selectedRequest.HostPlatform = pg::abi::Platform::kMacos;
    selectedRequest.CommandLineStyle = pg::abi::CommandLineStyle::kPosix;
    const auto units = pg::abi::LoadCompileUnits(selectedRequest, report);
    if (!units || units->size() != 2)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    std::vector<std::filesystem::path> objects;
    std::vector<std::vector<std::string>> commands;
    auto rows = nlohmann::json::array();
    std::string objectList;
    for (std::size_t index = 0; index < units->size(); ++index)
    {
        const auto& unit = units->at(index);
        const auto obj = work / ("unit-" + std::to_string(index) + ".o");
        objects.push_back(obj);
        objectList += obj.generic_string() + "\n";
        std::vector<std::string> argv{compiler.generic_string()};
        argv.insert(argv.end(), unit.AbiArguments.begin(), unit.AbiArguments.end());
        argv.insert(argv.end(), {"-c", unit.Source.generic_string(), "-o", obj.generic_string()});
        rows.push_back({
            {"directory", current.generic_string()},
            {"file", unit.Source.generic_string()},
            {"output", obj.generic_string()},
            {"arguments", argv},
        });
        commands.push_back(std::move(argv));
    }
    const auto collectionManifest = work / "objects.txt";
    const auto collectionDatabase = work / "database.json";
    const auto linkGraph = work / "links.json";
    const auto library = work / "libAppleCppCollectorCli.a";
    const auto requestFile = work / "request.json";
    const nlohmann::json dependencyGraph{
        {"schema", 1},
        {"format", "cmake-actual-v1"},
        {"configuration", "Release"},
        {"aliases", nlohmann::json::array()},
        {"targets", nlohmann::json::array({{
                        {"name", "AppleCppCollectorCli"},
                        {"kind", "module"},
                        {"language", "cxx"},
                        {"managed", true},
                        {"source_visible", true},
                        {"links", nlohmann::json::array()},
                    }})},
    };
    if (!Write(collectionManifest, objectList) || !Write(collectionDatabase, rows.dump()) ||
        !Write(linkGraph, dependencyGraph.dump()))
    {
        return 1;
    }
    const nlohmann::json request{
        {"schema", 2},
        {"source_root", root.generic_string()},
        {"build_root", build.generic_string()},
        {"target_name", "AppleCppCollectorCli"},
        {"configuration", "Release"},
        {"host_platform", "macos"},
        {"target_platform", platform},
        {"command_line_style", "posix"},
        {"compiler", compiler.generic_string()},
        {"database", collectionDatabase.generic_string()},
        {"object_manifest", collectionManifest.generic_string()},
        {"library", library.generic_string()},
        {"output_directory", output.generic_string()},
        {"schema_file", (root / "wiki/foundation/abi-profile-v1.schema.json").generic_string()},
        {"signature_header", (output / "inputs.h").generic_string()},
        {"link_graph", linkGraph.generic_string()},
        {"platform_context", nlohmann::json::object()},
        {"environment_baseline", nlohmann::json::object()},
    };
    if (!Write(requestFile, request.dump()) ||
        Run(tool, current, {"guard", "--request", requestFile.generic_string()}) != 0)
    {
        return 1;
    }
    std::vector<std::future<bool>> parallelCompiles;
    parallelCompiles.reserve(commands.size());
    for (const auto& command : commands)
    {
        parallelCompiles.push_back(std::async(std::launch::async,
                                              [&, command]
                                              {
                                                  std::vector<std::string> invocation{
                                                      "compile", "--request", requestFile.generic_string(), "--"};
                                                  invocation.insert(invocation.end(), command.begin(), command.end());
                                                  return Run(tool, current, std::move(invocation)) == 0;
                                              }));
    }
    for (auto& compiled : parallelCompiles)
    {
        if (!compiled.get())
        {
            std::cerr << "Concurrent Apple compiler wrapper lost its observation or receipt\n";
            return 1;
        }
    }
    std::vector<std::string> archive{"rcs", library.generic_string()};
    for (const auto& object : objects)
    {
        archive.push_back(object.generic_string());
    }
    if (Run(compiler.parent_path() / "llvm-ar", current, std::move(archive)) != 0 ||
        Run(tool, current, {"collect", "--request", requestFile.generic_string()}) != 0 ||
        !Completed(output, 2, platform) ||
        Run(tool, current, {"guard", "--request", requestFile.generic_string()}) != 0)
    {
        std::cerr << "Apple C++ CLI collection did not complete with real compiler receipts\n";
        return 1;
    }
    // Preserve ONLY test-owned snapshots before the later intentional receipt
    // invalidation revokes the live generation. A comparison CTest can then
    // independently prove macOS and iOS never share a Profile/Fingerprint.
    const auto completedProfile = pg::abi::ReadFile(output / "profile.json", "capture_profile", report);
    const auto completedFingerprint = pg::abi::ReadFile(output / "fingerprint.sha256", "capture_digest", report);
    if (!completedProfile || !completedFingerprint || !Write(work / "verified-profile.json", *completedProfile) ||
        !Write(work / "verified-fingerprint.sha256", *completedFingerprint))
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    const auto receiptRequest = pg::abi::ReadCollectionRequest(requestFile, report);
    const auto receiptUnits = receiptRequest ? pg::abi::LoadCompileUnits(*receiptRequest, report)
                                             : pg::Result<std::vector<pg::abi::CompileUnit>>{};
    if (!receiptUnits || receiptUnits->empty())
    {
        return 1;
    }
    std::filesystem::remove(pg::abi::CompileReceiptPath(*receiptRequest, receiptUnits->front()), error);
    if (error || Run(tool, current, {"collect", "--request", requestFile.generic_string()}) == 0 ||
        std::filesystem::exists(output / "complete.json"))
    {
        std::cerr << "Apple C++ CLI accepted a missing compiler receipt\n";
        return 1;
    }
    return 0;
#endif
}
