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

[[nodiscard]] bool Run(const std::filesystem::path& program, const std::filesystem::path& directory,
                       std::vector<std::string> arguments)
{
    pg::abi::FailureReport report;
    pg::abi::ProcessRequest process;
    process.Program = program;
    process.Directory = directory;
    process.Arguments = std::move(arguments);
    const auto result = pg::abi::RunProcess(process, report);
    if (!result || result->ExitCode != 0)
    {
        report.WriteJson(std::cerr);
        if (result)
        {
            std::cerr << "Process " << program.generic_string() << " exited " << result->ExitCode
                      << "\nstdout=" << result->Stdout << "\nstderr=" << result->Stderr << '\n';
        }
        return false;
    }
    return true;
}

[[nodiscard]] bool Completed(const std::filesystem::path& output)
{
    pg::abi::FailureReport report;
    const auto complete = pg::abi::ReadFile(output / "complete.json", "android_completed", report);
    const auto fingerprint = pg::abi::ReadFile(output / "fingerprint.sha256", "android_digest", report);
    const auto canonical = pg::abi::ReadFile(output / "canonical.json", "android_canonical", report);
    const auto profile = pg::abi::ReadFile(output / "profile.json", "android_profile", report);
    if (!complete || !fingerprint || !canonical || !profile)
    {
        report.WriteJson(std::cerr);
        return false;
    }
    const auto receipt = nlohmann::json::parse(*complete, nullptr, false);
    const auto parsed = nlohmann::json::parse(*profile, nullptr, false);
    return receipt.is_object() && !parsed.is_discarded() && fingerprint->size() == 64 &&
           receipt.contains("fingerprint") && receipt.at("fingerprint") == *fingerprint &&
           receipt.contains("receipts") && receipt.at("receipts").is_array() && receipt.at("receipts").size() == 2 &&
           parsed.at("target").at("triple") == "aarch64-none-linux-android29" &&
           parsed.at("stdlib").at("abi_namespace") == "__ndk1" &&
           parsed.at("language").at("abi_options").at(2) == "char_signedness=unsigned" &&
           parsed.at("runtime").at("sdk_abi_id") == "android-ndk-r30-api-29";
}
} // namespace

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
    const auto args = std::span(argv, static_cast<std::size_t>(argc));
    const auto root = std::filesystem::absolute(args.subspan<1>().front());
    const auto build = std::filesystem::absolute(args.subspan<2>().front());
    const auto compiler = std::filesystem::path(args.subspan<3>().front());
    const auto tool = std::filesystem::path(args.subspan<4>().front());
    const auto work = build / "Tests/AbiProfile/AndroidCppCollectorCli";
    const auto output = build / "Abi/Release/AndroidCppCollectorCli";
    std::error_code error;
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
    const auto file = pg::abi::ReadFile(build / "compile_commands.json", "android_cli_database", report);
    if (!file)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    const auto database = nlohmann::json::parse(*file, nullptr, false);
    if (!database.is_array())
    {
        return 1;
    }
    std::string originalObjects;
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
            originalObjects += entry.at("output").get<std::string>() + "\n";
        }
    }
    const auto originalManifest = work / "original-objects.txt";
    if (std::ranges::count(originalObjects, '\n') != 2 || !Write(originalManifest, originalObjects))
    {
        std::cerr << "Missing real Android ARM64 compilation command pair\n";
        return 1;
    }
    pg::abi::CollectionRequest selected;
    selected.SourceRoot = root;
    selected.BuildRoot = build;
    selected.Compiler = compiler;
    selected.Database = build / "compile_commands.json";
    selected.ObjectManifest = originalManifest;
    selected.HostPlatform = pg::abi::Platform::kWindows;
    selected.TargetPlatform = pg::abi::Platform::kAndroid;
    selected.CommandLineStyle = pg::abi::CommandLineStyle::kWindows;
    const auto units = pg::abi::LoadCompileUnits(selected, report);
    if (!units || units->size() != 2)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    std::vector<std::filesystem::path> objects;
    std::vector<std::vector<std::string>> commands;
    auto rows = nlohmann::json::array();
    std::string objectsText;
    for (std::size_t index = 0; index < units->size(); ++index)
    {
        const auto& unit = units->at(index);
        const auto object = work / ("unit-" + std::to_string(index) + ".o");
        objects.push_back(object);
        objectsText += object.generic_string() + "\n";
        std::vector<std::string> invocation{compiler.generic_string()};
        invocation.insert(invocation.end(), unit.AbiArguments.begin(), unit.AbiArguments.end());
        invocation.insert(invocation.end(), {
                                                "-c",
                                                unit.Source.generic_string(),
                                                "-o",
                                                object.generic_string(),
                                            });
        rows.push_back({
            {"directory", unit.Directory.generic_string()},
            {"file", unit.Source.generic_string()},
            {"output", object.generic_string()},
            {"arguments", invocation},
        });
        commands.push_back(std::move(invocation));
    }
    const auto manifest = work / "objects.txt";
    const auto newDatabase = work / "database.json";
    const auto graph = work / "links.json";
    const auto library = work / "libAndroidCppCollectorCli.a";
    const auto requestFile = work / "request.json";
    const nlohmann::json dependencyGraph{
        {"schema", 1},
        {"format", "cmake-actual-v1"},
        {"configuration", "Release"},
        {"aliases", nlohmann::json::array()},
        {"targets", nlohmann::json::array({{
                        {"name", "AndroidCppCollectorCli"},
                        {"kind", "module"},
                        {"language", "cxx"},
                        {"managed", true},
                        {"source_visible", true},
                        {"links", nlohmann::json::array()},
                    }})},
    };
    if (!Write(manifest, objectsText) || !Write(newDatabase, rows.dump()) || !Write(graph, dependencyGraph.dump()))
    {
        return 1;
    }
    const nlohmann::json request = {
        {"schema", 2},
        {"source_root", root.generic_string()},
        {"build_root", build.generic_string()},
        {"target_name", "AndroidCppCollectorCli"},
        {"configuration", "Release"},
        {"host_platform", "windows"},
        {"target_platform", "android"},
        {"command_line_style", "windows"},
        {"compiler", compiler.generic_string()},
        {"database", newDatabase.generic_string()},
        {"object_manifest", manifest.generic_string()},
        {"library", library.generic_string()},
        {"output_directory", output.generic_string()},
        {"schema_file", (root / "wiki/foundation/abi-profile-v1.schema.json").generic_string()},
        {"signature_header", (output / "inputs.h").generic_string()},
        {"link_graph", graph.generic_string()},
        {"platform_context", nlohmann::json::object()},
        {"environment_baseline", nlohmann::json::object()},
    };
    if (!Write(requestFile, request.dump()) || !Run(tool, build, {"guard", "--request", requestFile.generic_string()}))
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
                                                  return Run(tool, build, std::move(invocation));
                                              }));
    }
    for (auto& compiled : parallelCompiles)
    {
        if (!compiled.get())
        {
            std::cerr << "Concurrent Android compiler wrapper lost its observation or receipt\n";
            return 1;
        }
    }
    std::vector<std::string> ar{"rcs", library.generic_string()};
    for (const auto& object : objects)
    {
        ar.push_back(object.generic_string());
    }
    if (!Run(compiler.parent_path() / "llvm-ar.exe", build, std::move(ar)) ||
        !Run(tool, build, {"collect", "--request", requestFile.generic_string()}) || !Completed(output) ||
        !Run(tool, build, {"guard", "--request", requestFile.generic_string()}))
    {
        std::cerr << "Android NDK 21 C++ CLI transaction failed\n";
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
    if (error)
    {
        return 1;
    }
    // A second collect must fail closed and revoke the previously published generation.
    pg::abi::ProcessRequest invalid;
    invalid.Program = tool;
    invalid.Directory = build;
    invalid.Arguments = {"collect", "--request", requestFile.generic_string()};
    const auto rejected = pg::abi::RunProcess(invalid, report);
    if (!rejected || rejected->ExitCode == 0 || std::filesystem::exists(output / "complete.json"))
    {
        std::cerr << "Missing Android receipt did not revoke completed ABI generation\n";
        return 1;
    }
    return 0;
#endif
}
