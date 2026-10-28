#include "../../BuildTools/AbiProfile/Private/CollectionRequest.h"
#include "../../BuildTools/AbiProfile/Private/CollectionState.h"
#include "../../BuildTools/AbiProfile/Private/CompileCommands.h"
#include "../../BuildTools/AbiProfile/Private/DependencyGraph.h"
#include "../../BuildTools/AbiProfile/Private/Encoding.h"
#include "../../BuildTools/AbiProfile/Private/Process.h"
#include "../../BuildTools/AbiProfile/Private/ToolIo.h"
#include "../../BuildTools/AbiProfile/Private/ToolReport.h"
#include "Result.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <stop_token>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h> // IWYU pragma: keep - native environment tests.
#include <fileapi.h>
#include <handleapi.h>
#include <minwinbase.h>
#include <minwindef.h>
#include <processenv.h>
#include <stdlib.h> // NOLINT(modernize-deprecated-headers) - Windows _putenv_s declaration.
#include <winbase.h>
#include <winerror.h>
#include <winnt.h>
#endif
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace
{
// Fault injection stays in tests; production uses the same checked I/O boundary.
// Plain function pointers carry no context; atomics synchronize only this test fault fixture.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<int> gFault{0};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> gEntered{false};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> gRelease{false};
pg::Status FaultWrite(const std::filesystem::path& path, std::string_view text, pg::abi::FailureReport& report)
{
    if (path.filename() == "profile.json")
    {
        const auto fault = gFault.load();
        if (fault >= 1 && fault <= 3)
        {
            constexpr std::array kCodes{
                pg::abi::ToolError::kWriteOutput,
                pg::abi::ToolError::kFlushOutput,
                pg::abi::ToolError::kCloseFile,
            };
            const auto code = kCodes.at(static_cast<std::size_t>(fault - 1));
            return std::unexpected(report.Add(code, "injected_io_failure", path.generic_string(),
                                              std::make_error_code(std::errc::io_error)));
        }
        if (fault == 6)
        {
            gEntered = true;
            while (!gRelease.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
    return pg::abi::WriteFile(path, text, report);
}
pg::Status FaultRemove(const std::filesystem::path& path, pg::abi::FailureReport& report)
{
    if (gFault == 5 && path.filename() == ".collection-lock")
    {
        return std::unexpected(report.Add(pg::abi::ToolError::kRemoveOutput, "injected_cleanup_permission",
                                          path.generic_string(), std::make_error_code(std::errc::permission_denied)));
    }
    return pg::abi::RemoveGeneratedFile(path, report);
}
pg::Status FaultPublish(const std::filesystem::path& from, const std::filesystem::path& to,
                        pg::abi::FailureReport& report)
{
    if (gFault == 4 && to.filename() == "complete.json")
    {
        return std::unexpected(
            report.Add(pg::abi::ToolError::kWriteOutput, "injected_final_publish", to.generic_string()));
    }
    std::error_code error;
    std::filesystem::rename(from, to, error);
    return error ? pg::Status{std::unexpected(
                       report.Add(pg::abi::ToolError::kWriteOutput, "test_publish", to.generic_string(), error))}
                 : pg::Status{};
}
void SetEnvironment(const char* name, const char* value)
{
#ifdef _WIN32
    (void)::_putenv_s(name, value);
#else
    (void)::setenv(name, value, 1);
#endif
}
void Put(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary);
    file << text;
}
std::string Get(const std::filesystem::path& path)
{
    pg::abi::FailureReport report;
    const auto text = pg::abi::ReadFile(path, "test_read", report);
    return text ? *text : "";
}
bool Archive(const pg::abi::CollectionRequest& request, std::span<const std::filesystem::path> objects,
             std::string_view format = "gnu")
{
    std::filesystem::remove(request.Library);
    pg::abi::ProcessRequest command;
    command.Program = request.Compiler.parent_path() /
#ifdef _WIN32
                      "llvm-ar.exe";
#else
                      "llvm-ar";
#endif
    command.Arguments = {"rcs", "--format=" + std::string(format), request.Library.generic_string()};
    for (const auto& object : objects)
    {
        command.Arguments.push_back(object.generic_string());
    }
    command.Directory = request.BuildRoot;
    pg::abi::FailureReport report;
    const auto result = pg::abi::RunProcess(command, report);
    if (!result || result->ExitCode != 0)
    {
        report.WriteJson(std::cerr);
        return false;
    }
    return true;
}
bool Archive(const pg::abi::CollectionRequest& request, const std::filesystem::path& object,
             std::string_view format = "gnu")
{
    return Archive(request, std::span(&object, 1), format);
}
} // namespace
int main(int argc, char* const* argv)
{
    if (argc != 5)
    {
        return 2;
    }
    const auto args = std::span(argv, static_cast<std::size_t>(argc));
    // Each run owns a fresh fixture generation. A previously interrupted test
    // may leave a fail-closed .collection-lock; preserve it for diagnosis.
    const auto generation = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root =
        std::filesystem::absolute(args.subspan(1).front()) / "CollectionState" / ("run-" + std::to_string(generation));
    std::error_code createError;
    if (!std::filesystem::create_directories(root, createError) || createError)
    {
        std::cerr << "unable to create isolated state fixture: " << createError.message() << '\n';
        return 1;
    }
    pg::abi::CollectionRequest request;
    request.SourceRoot = root;
    request.BuildRoot = root;
    request.TargetName = "Fixture";
    request.Configuration = "Debug";
    request.OutputDirectory = root / "Abi/Debug/Fixture";
    request.SignatureHeader = request.OutputDirectory / "inputs.h";
    request.Compiler = std::filesystem::absolute(args.subspan(2).front());
    request.Database = root / "compile_commands.json";
    request.ObjectManifest = root / "objects.txt";
    request.SchemaFile = std::filesystem::absolute(args.subspan(3).front()) / "abi-profile-v1.schema.json";
    request.Library = root / "fixture.a";
    request.LinkGraph = root / "links.json";
    request.PlatformContext = nlohmann::json::object();
#ifdef _WIN32
    request.HostPlatform = pg::abi::Platform::kWindows;
    request.TargetPlatform = pg::abi::Platform::kWindows;
    request.CommandLineStyle = pg::abi::CommandLineStyle::kWindows;
#else
    request.HostPlatform = pg::abi::Platform::kLinux;
    request.TargetPlatform = pg::abi::Platform::kLinux;
    request.CommandLineStyle = pg::abi::CommandLineStyle::kPosix;
#endif
    const auto source = root / "unit.cpp";
    const auto header = root / "unit.h";
    const auto object = root / "unit.o";
    Put(source, "#include \"unit.h\"\nint value() { return kValue; }\n");
    Put(header, "constexpr int kValue = 1;\n");
    Put(request.LinkGraph, "{}");
    const std::vector<std::string> command{
        request.Compiler.generic_string(), "-c", source.generic_string(), "-o", object.generic_string(),
    };
    const auto database = nlohmann::json::array({
        {
            {"directory", root.generic_string()},
            {"file", source.generic_string()},
            {"output", object.generic_string()},
            {"arguments", command},
        },
    });
    Put(request.Database, database.dump());
    Put(request.ObjectManifest, object.generic_string() + "\n");
    // This test directory is private to this CTest. Remove only known generated files.
    std::filesystem::create_directories(request.OutputDirectory);
    for (const auto* name :
         {"complete.json", "profile.json", "canonical.json", "fingerprint.sha256", "evidence.json", "inputs.h"})
    {
        std::filesystem::remove(request.OutputDirectory / name);
    }
    std::filesystem::remove(object);
    pg::abi::CompileUnit initialUnit;
    initialUnit.Object = object;
    std::filesystem::remove(pg::abi::CompileReceiptPath(request, initialUnit));
    std::filesystem::remove(request.OutputDirectory / "archive-binding.json");
    int failures = 0;
    const auto check = [&failures](bool passed, const char* name)
    {
        if (!passed)
        {
            std::cerr << "FAIL: " << name << '\n';
            ++failures;
        }
    };
    pg::abi::FailureReport report;
    for (const auto& input : {source, header})
    {
        auto conflict = request;
        conflict.SignatureHeader = input;
        const auto originalInput = Get(input);
        pg::abi::FailureReport protectedInput;
        check(!pg::abi::GuardCollection(conflict, protectedInput) && Get(input) == originalInput,
              "signature alias preserves source and header before initial generation write");
    }
    auto protectedRequest = request;
    protectedRequest.RequestFile = request.OutputDirectory / ".invalid";
    Put(protectedRequest.RequestFile, "preserve request bytes");
    pg::abi::FailureReport requestAlias;
    check(!pg::abi::InvalidateCollection(protectedRequest, requestAlias) &&
              Get(protectedRequest.RequestFile) == "preserve request bytes",
          "lifecycle retains parsed request identity and refuses auxiliary alias");
    std::filesystem::remove(protectedRequest.RequestFile);
    for (const auto* reserved : {".invalid", "archive-binding.json", "group-complete.json", "complete.json"})
    {
        auto conflict = request;
        conflict.SignatureHeader = request.OutputDirectory / reserved;
        pg::abi::FailureReport reservedSignature;
        check(!pg::abi::GuardCollection(conflict, reservedSignature) &&
                  !std::filesystem::exists(conflict.SignatureHeader),
              "lifecycle refuses signature aliases of all auxiliary output files before writing");
    }
    const auto first = pg::abi::GuardCollection(request, report);
    if (!first)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    check(*first == pg::abi::GuardDisposition::kBuildRequired && std::filesystem::exists(request.SignatureHeader),
          "first Guard requires build and writes generation dependency");
    const auto signature = Get(request.SignatureHeader);
    check(signature.starts_with("//") && !signature.contains('#'), "signature is comment only");
    const auto units = pg::abi::LoadCompileUnits(request, report);
    if (!units)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    pg::abi::CompileObservation observation;
    observation.Headers = {header, request.SignatureHeader};
    check(pg::abi::RunObservedCompilerInvocation(request, units->front(), observation, report) == 0,
          "real Clang success creates receipt");
    const auto snapshot = pg::abi::SnapshotInputs(request, *units, observation, report);
    check(snapshot.has_value(), "snapshot succeeds");
    if (!snapshot)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    auto emptyEnvironment = request;
    emptyEnvironment.EnvironmentBaseline["PANGU_REVIEW_UNSET_STATE"] = "";
    SetEnvironment("PANGU_REVIEW_UNSET_STATE", "");
#ifndef _WIN32
    (void)::unsetenv("PANGU_REVIEW_UNSET_STATE");
#endif
    pg::abi::FailureReport unsetEnvironment;
    check(!pg::abi::SnapshotInputs(emptyEnvironment, *units, observation, unsetEnvironment),
          "unset environment differs from configured empty string");
    emptyEnvironment.EnvironmentBaseline["PANGU_REVIEW_UNSET_STATE"] = std::nullopt;
    const auto unsetSnapshot = pg::abi::SnapshotInputs(emptyEnvironment, *units, observation, report);
    check(unsetSnapshot && unsetSnapshot->Environment.at("PANGU_REVIEW_UNSET_STATE").is_null(),
          "unset environment snapshot preserves JSON null");
#ifdef _WIN32
    (void)::SetEnvironmentVariableA("PANGU_REVIEW_UNSET_STATE", "");
#else
    (void)::setenv("PANGU_REVIEW_UNSET_STATE", "", 1);
#endif
    emptyEnvironment.EnvironmentBaseline["PANGU_REVIEW_UNSET_STATE"] = "";
    const auto emptySnapshot = pg::abi::SnapshotInputs(emptyEnvironment, *units, observation, report);
    if (!emptySnapshot)
    {
        report.WriteJson(std::cerr);
    }
    check(emptySnapshot && emptySnapshot->Environment.at("PANGU_REVIEW_UNSET_STATE") == std::string{} &&
              unsetSnapshot && emptySnapshot->CompileDigest != unsetSnapshot->CompileDigest,
          "set-empty environment snapshot differs from unset");
    emptyEnvironment.EnvironmentBaseline["PANGU_REVIEW_UNSET_STATE"] = std::nullopt;
    pg::abi::FailureReport emptyMismatch;
    check(!pg::abi::SnapshotInputs(emptyEnvironment, *units, observation, emptyMismatch),
          "set-empty environment differs from configured unset");
#ifdef _WIN32
    (void)::SetEnvironmentVariableW(L"PANGU_REVIEW_UNSET_STATE", L"\u76ee\u5f55");
    emptyEnvironment.EnvironmentBaseline["PANGU_REVIEW_UNSET_STATE"] = "目录";
    check(pg::abi::SnapshotInputs(emptyEnvironment, *units, observation, report).has_value(),
          "Windows native environment is decoded as UTF-8");
    (void)::SetEnvironmentVariableA("PANGU_REVIEW_UNSET_STATE", nullptr);
#else
    (void)::unsetenv("PANGU_REVIEW_UNSET_STATE");
#endif
    const auto vectors =
        nlohmann::json::parse(Get(std::filesystem::absolute(args.subspan(3).front()) / "abi-profile-v1.vectors.json"));
    pg::abi::CollectedProfile profile;
    profile.Profile = vectors.at("baseline").at("profile");
    profile.Evidence = {{"fixture", true}};
    Put(request.Library, "!<arch>\n");
    pg::abi::FailureReport absentMember;
    check(!pg::abi::PublishCollection(request, profile, *snapshot, absentMember),
          "first collection rejects archive missing the compiled object");
    std::filesystem::remove(request.Library);
    check(Archive(request, object), "create real LLVM archive");
    const auto wrongObject = root / "wrong.o";
    Put(wrongObject, "a different object");
    check(Archive(request, wrongObject), "create mismatched archive");
    pg::abi::FailureReport firstMismatch;
    check(!pg::abi::PublishCollection(request, profile, *snapshot, firstMismatch),
          "first collection rejects a different object in archive");
    check(Archive(request, object), "restore matching archive");
    const auto originalArchive = Get(request.Library);
    const auto longObject = root / "object_with_a_name_longer_than_the_archive_header_field.o";
    Put(longObject, Get(object));
    for (const auto* format : {"gnu", "bsd", "darwin"})
    {
        check(Archive(request, longObject, format), "generate actual LLVM long-name archive format");
        pg::abi::FailureReport archiveFormat;
        const auto formatPublished = pg::abi::PublishCollection(request, profile, *snapshot, archiveFormat);
        if (!formatPublished)
        {
            std::cerr << "archive format: " << format << '\n';
            archiveFormat.WriteJson(std::cerr);
        }
        check(formatPublished.has_value(), "GNU and BSD extended names bind exact current object bytes");
    }
    const std::array extraObjects{object, wrongObject};
    check(Archive(request, extraObjects), "generate archive containing extra object");
    pg::abi::FailureReport extraMember;
    check(!pg::abi::PublishCollection(request, profile, *snapshot, extraMember),
          "extra archive member blocks publication");
    for (const auto& invalidArchive : {std::string("!<thin>\n"), originalArchive.substr(0, originalArchive.size() - 3)})
    {
        Put(request.Library, invalidArchive);
        pg::abi::FailureReport malformedArchive;
        check(!pg::abi::PublishCollection(request, profile, *snapshot, malformedArchive),
              "thin and truncated archive formats fail closed");
    }
    Put(request.Library, originalArchive);
    check(pg::abi::PublishCollection(request, profile, *snapshot, report).has_value(),
          "publish after genuine compile receipt");
    const auto current = pg::abi::GuardCollection(request, report);
    check(current && *current == pg::abi::GuardDisposition::kCurrent, "unchanged Guard uses checked content cache");
    // Duplicate basenames are valid across compile directories. Both entries
    // must be consumed once, even when an archive name map would collapse them.
    auto duplicateRequest = request;
    duplicateRequest.TargetName = "DuplicateMembers";
    duplicateRequest.OutputDirectory = root / "Abi/Debug/DuplicateMembers";
    duplicateRequest.SignatureHeader = duplicateRequest.OutputDirectory / "inputs.h";
    duplicateRequest.Database = root / "duplicate-commands.json";
    duplicateRequest.ObjectManifest = root / "duplicate-objects.txt";
    duplicateRequest.Library = root / "duplicates.a";
    const std::array duplicateObjects{root / "first/unit.o", root / "second/unit.o"};
    auto duplicateDatabase = nlohmann::json::array();
    std::string duplicateManifest;
    for (const auto& duplicateObject : duplicateObjects)
    {
        std::filesystem::create_directories(duplicateObject.parent_path());
        const auto duplicateSource = duplicateObject.parent_path() / "unit.cpp";
        Put(duplicateSource, "int duplicate_value() { return 1; }\n");
        const std::vector<std::string> duplicateCommand{
            request.Compiler.generic_string(), "-c", duplicateSource.generic_string(), "-o",
            duplicateObject.generic_string(),
        };
        duplicateDatabase.push_back({
            {"directory", root.generic_string()},
            {"file", duplicateSource.generic_string()},
            {"output", duplicateObject.generic_string()},
            {"arguments", duplicateCommand},
        });
        duplicateManifest += duplicateObject.generic_string() + "\n";
    }
    Put(duplicateRequest.Database, duplicateDatabase.dump());
    Put(duplicateRequest.ObjectManifest, duplicateManifest);
    pg::abi::FailureReport duplicateReport;
    check(pg::abi::GuardCollection(duplicateRequest, duplicateReport).has_value(), "duplicate-member initial Guard");
    const auto duplicateUnits = pg::abi::LoadCompileUnits(duplicateRequest, duplicateReport);
    pg::abi::CompileObservation duplicateObservation;
    duplicateObservation.Headers = {header, duplicateRequest.SignatureHeader};
    if (duplicateUnits)
    {
        for (const auto& unit : *duplicateUnits)
        {
            check(pg::abi::RunObservedCompilerInvocation(duplicateRequest, unit, duplicateObservation,
                                                         duplicateReport) == 0,
                  "duplicate member has genuine object receipt");
        }
        const auto duplicateSnapshot =
            pg::abi::SnapshotInputs(duplicateRequest, *duplicateUnits, duplicateObservation, duplicateReport);
        for (const auto* format : {"gnu", "bsd", "darwin"})
        {
            check(Archive(duplicateRequest, duplicateObjects, format), "archive duplicate basenames with LLVM");
            check(duplicateSnapshot &&
                      pg::abi::PublishCollection(duplicateRequest, profile, *duplicateSnapshot, duplicateReport)
                          .has_value(),
                  "duplicate member names preserve multiset of object bytes");
        }
        check(Archive(duplicateRequest, duplicateObjects.front()), "archive only one duplicate member");
        pg::abi::FailureReport missingDuplicate;
        check(duplicateSnapshot &&
                  !pg::abi::PublishCollection(duplicateRequest, profile, *duplicateSnapshot, missingDuplicate),
              "one archive member cannot satisfy two compile receipts");
    }
    else
    {
        duplicateReport.WriteJson(std::cerr);
        check(false, "load duplicate-name compile units");
    }
    pg::abi::DependencyGraph ownStaticGraph;
    pg::abi::DependencyTarget ownStatic;
    ownStatic.Name = request.TargetName;
    ownStatic.Kind = pg::abi::DependencyKind::kStatic;
    ownStatic.Language = "cxx";
    ownStatic.SourceVisible = true;
    ownStaticGraph.Targets.emplace(request.TargetName, std::move(ownStatic));
    const auto ownStaticPlan = pg::abi::PlanDependencies(ownStaticGraph, request.TargetName, report);
    pg::abi::FailureReport selfReport;
    check(ownStaticPlan &&
              pg::abi::VerifyDependencyProfiles(ownStaticGraph, *ownStaticPlan, request, profile.Profile, selfReport),
          "STATIC root checks its current collection instead of requiring a preexisting self-completion");
    // This test binary created genuine receipts and complete.json itself,
    // so GuardCollection validates the same tool identity as the publisher.
    const auto staticRequestFile = root / "fixture-static-request.json";
#ifdef _WIN32
    constexpr auto kTestHost = "windows";
    constexpr auto kQuoting = "windows";
#else
    constexpr auto kTestHost = "linux";
    constexpr auto kQuoting = "posix";
#endif
    const nlohmann::json staticRequest{
        {"schema", 2},
        {"source_root", request.SourceRoot.generic_string()},
        {"build_root", request.BuildRoot.generic_string()},
        {"target_name", request.TargetName},
        {"configuration", request.Configuration},
        {"host_platform", kTestHost},
        {"target_platform", kTestHost},
        {"command_line_style", kQuoting},
        {"compiler", request.Compiler.generic_string()},
        {"database", request.Database.generic_string()},
        {"object_manifest", request.ObjectManifest.generic_string()},
        {"library", request.Library.generic_string()},
        {"output_directory", request.OutputDirectory.generic_string()},
        {"schema_file", request.SchemaFile.generic_string()},
        {"signature_header", request.SignatureHeader.generic_string()},
        {"link_graph", request.LinkGraph.generic_string()},
        {"platform_context", nlohmann::json::object()},
        {"environment_baseline", nlohmann::json::object()},
    };
    Put(staticRequestFile, staticRequest.dump());
    pg::abi::DependencyGraph certifiedGraph;
    pg::abi::DependencyTarget consumer;
    consumer.Name = "Consumer";
    consumer.Kind = pg::abi::DependencyKind::kModule;
    consumer.Managed = true;
    consumer.SourceVisible = true;
    consumer.Language = "cxx";
    consumer.Links.push_back({.Target = "Fixture", .Scope = "private"});
    certifiedGraph.Targets.emplace("Consumer", std::move(consumer));
    pg::abi::DependencyTarget staticTarget;
    staticTarget.Name = "Fixture";
    staticTarget.Kind = pg::abi::DependencyKind::kStatic;
    staticTarget.SourceVisible = true;
    staticTarget.Language = "cxx";
    staticTarget.RequestFile = staticRequestFile;
    certifiedGraph.Targets.emplace("Fixture", std::move(staticTarget));
    const auto certifiedPlan = pg::abi::PlanDependencies(certifiedGraph, "Consumer", report);
    auto parentRequest = request;
    parentRequest.TargetName = "Consumer";
    parentRequest.Library = root / "consumer.a";
    parentRequest.OutputDirectory = root / "Abi/Debug/Consumer";
    parentRequest.SignatureHeader = parentRequest.OutputDirectory / "inputs.h";
    pg::abi::FailureReport attestationReport;
    check(certifiedPlan && pg::abi::VerifyDependencyProfiles(certifiedGraph, *certifiedPlan, parentRequest,
                                                             profile.Profile, attestationReport),
          "independently completed STATIC admitted by matching provenance");
    auto incorrectBuild = parentRequest;
    incorrectBuild.Configuration = "Release";
    pg::abi::FailureReport wrongBuild;
    check(certifiedPlan &&
              !pg::abi::VerifyDependencyProfiles(certifiedGraph, *certifiedPlan, incorrectBuild, profile.Profile,
                                                 wrongBuild) &&
              wrongBuild.Count == 1 && wrongBuild.Entries.front().Stage.View() == "dependency_request_identity",
          "an independently completed STATIC from another configuration cannot be relabelled");
    auto missingRequestGraph = certifiedGraph;
    missingRequestGraph.Targets.at("Fixture").RequestFile.clear();
    pg::abi::FailureReport missingRequest;
    check(certifiedPlan &&
              !pg::abi::VerifyDependencyProfiles(missingRequestGraph, *certifiedPlan, parentRequest, profile.Profile,
                                                 missingRequest) &&
              missingRequest.Count == 1 && missingRequest.Entries.front().Stage.View() == "dependency_request_missing",
          "missing STATIC collection request fails closed");
    auto incompatibleProfile = profile.Profile;
    incompatibleProfile.at("stdlib").at("iterator_debug_level") = "2";
    pg::abi::FailureReport incompatibleReport;
    check(certifiedPlan &&
              !pg::abi::VerifyDependencyProfiles(certifiedGraph, *certifiedPlan, parentRequest, incompatibleProfile,
                                                 incompatibleReport) &&
              incompatibleReport.Count == 1 &&
              incompatibleReport.Entries.front().JsonPointer.View() == "/stdlib/iterator_debug_level",
          "genuine STATIC profile difference reports precise ABI field");

    const auto consumerGraphPath = root / "consumer-graph.json";
    const nlohmann::json consumerGraph{
        {"schema", 1},
        {"format", "cmake-actual-v1"},
        {"configuration", "Debug"},
        {"aliases", nlohmann::json::array()},
        {
            "targets",
            nlohmann::json::array({
                {
                    {"name", "Consumer"},
                    {"kind", "module"},
                    {"language", "cxx"},
                    {"managed", true},
                    {"source_visible", true},
                    {"links", nlohmann::json::array({{{"target", "Fixture"}, {"scope", "private"}}})},
                },
                {
                    {"name", "Fixture"},
                    {"kind", "static"},
                    {"language", "cxx"},
                    {"managed", false},
                    {"source_visible", true},
                    {"request_file", staticRequestFile.generic_string()},
                    {"links", nlohmann::json::array()},
                },
            }),
        },
    };
    Put(consumerGraphPath, consumerGraph.dump());
    auto snapshotConsumer = parentRequest;
    snapshotConsumer.LinkGraph = consumerGraphPath;
    const auto beforeStaticChange = pg::abi::SnapshotInputs(snapshotConsumer, *units, observation, report);
    check(beforeStaticChange.has_value(), "consumer snapshot with STATIC request metadata");
    Put(request.Library, "library B");
    const auto afterStaticChange = pg::abi::SnapshotInputs(snapshotConsumer, *units, observation, report);
    check(beforeStaticChange && afterStaticChange &&
              beforeStaticChange->CompileDigest == afterStaticChange->CompileDigest &&
              beforeStaticChange->DependencyDigest != afterStaticChange->DependencyDigest,
          "technical STATIC archive replacement invalidates only dependency digest");
    Put(request.Library, originalArchive);

    const auto fingerprint = Get(request.OutputDirectory / "fingerprint.sha256");
    const auto receiptFile = pg::abi::CompileReceiptPath(request, units->front());
    const auto originalReceipt = Get(receiptFile);
    auto malformedReceipt = nlohmann::json::parse(originalReceipt);
    malformedReceipt.at("object_sha256") = 42;
    Put(receiptFile, malformedReceipt.dump());
    pg::abi::FailureReport malformed;
    check(!pg::abi::GuardCollection(request, malformed) && malformed.Count > 0,
          "wrong JSON type in object receipt is a reported failure");
    Put(receiptFile, originalReceipt);
    check(pg::abi::PublishCollection(request, profile, *snapshot, report).has_value(), "restore typed receipt");
    const auto complete = nlohmann::json::parse(Get(request.OutputDirectory / "complete.json"), nullptr, false);
    check(!complete.is_discarded() && complete.contains("library_sha256") && complete.contains("canonical_sha256") &&
              complete.contains("snapshot"),
          "complete binds artifact and input content");
    const auto libraryTime = std::filesystem::last_write_time(request.Library);
    Put(request.Library, "library B");
    std::filesystem::last_write_time(request.Library, libraryTime);
    pg::abi::FailureReport replaced;
    check(!pg::abi::GuardCollection(request, replaced), "same size mtime library replacement is untrusted");
    check(!std::filesystem::exists(request.OutputDirectory / "complete.json"), "bad library revokes completion");
    Put(request.Library, originalArchive);
    check(pg::abi::PublishCollection(request, profile, *snapshot, report).has_value(),
          "restore trusted library permits revalidation");
    Put(request.OutputDirectory / "profile.json", "{}");
    pg::abi::FailureReport replacedProfile;
    check(!pg::abi::GuardCollection(request, replacedProfile), "profile replacement rejected");
    check(pg::abi::PublishCollection(request, profile, *snapshot, report).has_value(), "profile restore");
    const auto objectBytes = Get(object);
    Put(object, std::string(objectBytes.size(), 'x'));
    pg::abi::FailureReport replacedObject;
    check(!pg::abi::GuardCollection(request, replacedObject), "object replacement rejected");
    check(!pg::abi::PublishCollection(request, profile, *snapshot, replacedObject), "object cannot be relabelled");
    Put(object, objectBytes);
    std::filesystem::remove(pg::abi::CompileReceiptPath(request, units->front()));
    pg::abi::FailureReport missing;
    check(!pg::abi::PublishCollection(request, profile, *snapshot, missing), "missing receipt blocks publishing");
    check(!std::filesystem::exists(request.OutputDirectory / "complete.json"), "failed publish no complete");
    check(pg::abi::RunObservedCompilerInvocation(request, units->front(), observation, report) == 0,
          "recompile restores provenance");
    check(Archive(request, object), "archive restored object");
    check(pg::abi::PublishCollection(request, profile, *snapshot, report).has_value(), "recompiled publish");
    Put(source, "#include \"unit.h\"\nint value() { return kValue + 1; }\n");
    check(pg::abi::RunObservedCompilerInvocation(request, units->front(), observation, report) == 0,
          "genuine implementation change compiles without archiving");
    const auto unarchivedSnapshot = pg::abi::SnapshotInputs(request, *units, observation, report);
    pg::abi::FailureReport skippedArchive;
    check(unarchivedSnapshot && !pg::abi::PublishCollection(request, profile, *unarchivedSnapshot, skippedArchive) &&
              !std::filesystem::exists(request.OutputDirectory / "complete.json"),
          "new compile receipt cannot attest old archive members");
    Put(source, "#include \"unit.h\"\nint value() { return kValue; }\n");
    check(pg::abi::RunObservedCompilerInvocation(request, units->front(), observation, report) == 0 &&
              Archive(request, object),
          "restore actual compile and archive after skipped archive");
    check(pg::abi::PublishCollection(request, profile, *snapshot, report).has_value(),
          "restore after archive mismatch");
    Put(header, "constexpr int kValue = 2;\n");
    const auto changed = pg::abi::GuardCollection(request, report);
    check(changed && *changed == pg::abi::GuardDisposition::kBuildRequired, "ordinary header change requires rebuild");
    check(Get(request.SignatureHeader) != signature, "rebuild changes generation");
    const auto generationAfterChange = Get(request.SignatureHeader);
    Put(header, "constexpr int kValue = 3;\n");
    const auto secondChange = pg::abi::GuardCollection(request, report);
    check(secondChange && *secondChange == pg::abi::GuardDisposition::kBuildRequired &&
              Get(request.SignatureHeader) != generationAfterChange,
          "second input change while incomplete advances generation");
    const auto changedSnapshot = pg::abi::SnapshotInputs(request, *units, observation, report);
    pg::abi::FailureReport stale;
    check(changedSnapshot && !pg::abi::PublishCollection(request, profile, *changedSnapshot, stale),
          "old objects cannot get new input label");
    Put(header, "constexpr int kValue = 1;\n");
    pg::abi::FailureReport restoreReport;
    const auto restoredCode =
        pg::abi::RunObservedCompilerInvocation(request, units->front(), observation, restoreReport);
    if (restoredCode != 0)
    {
        std::cerr << "restore compiler exit=" << restoredCode << '\n';
        restoreReport.WriteJson(std::cerr);
        return 1;
    }
    check(Archive(request, object), "archive recompiled object");
    check(pg::abi::PublishCollection(request, profile, *snapshot, report).has_value(), "restore publish");
    const auto beforeDependency = Get(request.SignatureHeader);
    Put(request.LinkGraph, "{\"dependency_build\":2}");
    const auto dependency = pg::abi::GuardCollection(request, report);
    check(dependency && *dependency == pg::abi::GuardDisposition::kRecollectRequired &&
              Get(request.SignatureHeader) == beforeDependency,
          "dependency-only change preserves own generation for recollection");
    const auto dependencySnapshot = pg::abi::SnapshotInputs(request, *units, observation, report);
    check(dependencySnapshot && dependencySnapshot->CompileDigest == snapshot->CompileDigest &&
              dependencySnapshot->DependencyDigest != snapshot->DependencyDigest,
          "own and dependency digests separate");
    check(dependencySnapshot && pg::abi::PublishCollection(request, profile, *dependencySnapshot, report).has_value(),
          "compatible dependency refresh reuses own receipt");
    check(Get(request.OutputDirectory / "fingerprint.sha256") == fingerprint,
          "dependency implementation does not change ABI fingerprint");
    // Replacement must not be accepted merely by calling Publish after Guard.
    Put(request.Library, "library X");
    pg::abi::FailureReport relabel;
    check(!pg::abi::PublishCollection(request, profile, *dependencySnapshot, relabel),
          "unexplained library replacement cannot be relabelled");
    check(Archive(request, object), "restore archive contents");
    check(pg::abi::PublishCollection(request, profile, *dependencySnapshot, report).has_value(),
          "restore previous archive");
    SetEnvironment("PANGU_UNRELATED_STATE_TEST", "changed");
    const auto unrelated = pg::abi::GuardCollection(request, report);
    check(unrelated && *unrelated == pg::abi::GuardDisposition::kCurrent &&
              Get(request.OutputDirectory / "fingerprint.sha256") == fingerprint,
          "unrelated environment keeps cache and fingerprint");
    request.EnvironmentBaseline["PANGU_RELEVANT_STATE_TEST"] = "configured";
    SetEnvironment("PANGU_RELEVANT_STATE_TEST", "changed");
    pg::abi::FailureReport environment;
    check(!pg::abi::GuardCollection(request, environment), "configured environment mismatch requires reconfigure");
    request.EnvironmentBaseline.clear();
    const pg::abi::CollectionIo faults{.Write = &FaultWrite, .Remove = &FaultRemove, .Publish = &FaultPublish};
    for (int fault = 1; fault <= 5; ++fault)
    {
        gFault = fault;
        pg::abi::FailureReport failure;
        check(!pg::abi::PublishCollection(request, profile, *dependencySnapshot, failure, faults) &&
                  failure.Count > 0 && !std::filesystem::exists(request.OutputDirectory / "complete.json"),
              "write flush close final publish cleanup failure revokes complete");
        if (fault == 5)
        {
            check(failure.CleanupIncomplete, "cleanup permission observable");
        }
        gFault = 0;
        std::filesystem::remove(request.OutputDirectory / ".collection-lock");
    }
    gFault = 6;
    gEntered = false;
    gRelease = false;
    bool winner = true;
    std::jthread writer(
        [&]
        {
            pg::abi::FailureReport firstReport;
            winner = pg::abi::PublishCollection(request, profile, *dependencySnapshot, firstReport, faults).has_value();
        });
    const auto writerDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!gEntered.load() && std::chrono::steady_clock::now() < writerDeadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!gEntered.load())
    {
        gRelease = true;
        writer.join();
        std::cerr << "FAIL: publication worker did not reach its synchronized write\n";
        return 1;
    }
    pg::abi::FailureReport competitor;
    check(!pg::abi::PublishCollection(request, profile, *dependencySnapshot, competitor),
          "concurrent second writer fails visibly");
    gRelease = true;
    writer.join();
    gFault = 0;
    check(!winner && !std::filesystem::exists(request.OutputDirectory / "complete.json"),
          "conflicting generation never complete");
    auto conflict = request;
    conflict.SignatureHeader = request.OutputDirectory / "profile.json/generated.h";
    pg::abi::FailureReport ancestor;
    check(!pg::abi::GuardCollection(conflict, ancestor), "output ancestor conflict rejected");
    // Hand encoding does not possess the object/collection completion capability.
    Put(root / "manual.json", profile.Profile.dump());
    check(pg::abi::EncodeProfileFiles(request.SchemaFile, root / "manual.json", root / "manual-canonical.json",
                                      root / "manual-sha.txt", report) == 0 &&
              !std::filesystem::exists(request.OutputDirectory / "complete.json"),
          "manual encoder never completes a collection");
    // The database and launcher may use different response files with equivalent
    // effective argv; both identities and bytes must remain in the evidence.
    const auto databaseResponse = root / "database.rsp";
    const auto actualResponse = root / "actual.rsp";
    const auto responseText =
        std::string("-c \"") + source.generic_string() + "\" -o \"" + object.generic_string() + "\"";
    Put(databaseResponse, responseText);
    Put(actualResponse, responseText);
    const std::vector<std::string> databaseCommand{
        request.Compiler.generic_string(),
        "@" + databaseResponse.generic_string(),
    };
    Put(request.Database, nlohmann::json::array({
                                                    {
                                                        {"directory", root.generic_string()},
                                                        {"file", source.generic_string()},
                                                        {"output", object.generic_string()},
                                                        {"arguments", databaseCommand},
                                                    },
                                                })
                              .dump());
    const std::vector<std::string> actualCommand{
        request.Compiler.generic_string(),
        "@" + actualResponse.generic_string(),
    };
    const auto actualUnit = pg::abi::MatchCompilerInvocation(request, actualCommand, root, report);
    const auto responseUnits = pg::abi::LoadCompileUnits(request, report);
    check(actualUnit && pg::abi::RunObservedCompilerInvocation(request, *actualUnit, observation, report) == 0,
          "actual rsp invocation creates real object");
    check(Archive(request, object), "archive response-file compilation");
    const auto responseSnapshot = pg::abi::SnapshotInputs(request, *responseUnits, observation, report);
    check(responseSnapshot && pg::abi::PublishCollection(request, profile, *responseSnapshot, report).has_value(),
          "different database and invocation response files accepted");
    Put(actualResponse, responseText + " ");
    pg::abi::FailureReport actualResponseChanged;
    check(!pg::abi::GuardCollection(request, actualResponseChanged),
          "actual invocation response content replacement rejected");
    Put(actualResponse, responseText);
    check(pg::abi::PublishCollection(request, profile, *responseSnapshot, report).has_value(),
          "restored actual response permits verification");
    Put(databaseResponse, responseText + " ");
    const auto databaseChanged = pg::abi::GuardCollection(request, report);
    check(databaseChanged && *databaseChanged == pg::abi::GuardDisposition::kBuildRequired,
          "database response bytes change compile context");
    // A real controlled child makes the no-process Guard invariant observable.
    const auto archiveRequest = request;
    request.Compiler = std::filesystem::absolute(args.subspan(4).front());
    auto fixtureCommand = command;
    fixtureCommand.front() = request.Compiler.generic_string();
    fixtureCommand.insert(fixtureCommand.begin() + 1, "compile-fixture");
    const auto writeCommand = [&](const std::vector<std::string>& invocation)
    {
        Put(request.Database, nlohmann::json::array({
                                                        {
                                                            {"directory", root.generic_string()},
                                                            {"file", source.generic_string()},
                                                            {"output", object.generic_string()},
                                                            {"arguments", invocation},
                                                        },
                                                    })
                                  .dump());
    };
    writeCommand(fixtureCommand);
    const auto fixtureUnits = pg::abi::LoadCompileUnits(request, report);
    check(fixtureUnits &&
              pg::abi::RunObservedCompilerInvocation(request, fixtureUnits->front(), observation, report) == 0,
          "controlled real process receipt");
#ifdef _WIN32
    // Pause the real child after the initial state transition. Its parent must
    // subsequently reacquire the same target lock to publish the receipt.
    fixtureCommand.at(1) = "pause-fixture";
    writeCommand(fixtureCommand);
    const auto pausedUnits = pg::abi::LoadCompileUnits(request, report);
    if (!pausedUnits)
    {
        return 1;
    }
    const auto checkPendingLock = [&](bool releaseBeforeDeadline, bool fullReport)
    {
        const auto resume = std::filesystem::path(object.string() + ".resume");
        std::filesystem::remove(resume);
        const auto beforePause = Get(object.string() + ".invoked");
        pg::abi::FailureReport pendingReport;
        if (fullReport)
        {
            for (std::size_t index = 0; index < pg::abi::FailureReport::kCapacity; ++index)
            {
                (void)pendingReport.Add(pg::abi::ToolError::kInvalidRequest, "retained_before_lock");
            }
        }
        std::atomic<bool> finished{false};
        int pendingExit = -1;
        std::jthread pausedCompiler(
            [&]
            {
                pendingExit =
                    pg::abi::RunObservedCompilerInvocation(request, pausedUnits->front(), observation, pendingReport);
                finished = true;
            });
        const auto pauseDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!finished && Get(object.string() + ".invoked") == beforePause &&
               std::chrono::steady_clock::now() < pauseDeadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto lock = request.OutputDirectory / ".collection-lock";
        std::error_code pendingError;
        const bool lockCreated = !finished && Get(object.string() + ".invoked") != beforePause &&
                                 std::filesystem::create_directory(lock, pendingError);
        auto* const pendingHandle = lockCreated
                                        ? ::CreateFileW(lock.c_str(), DELETE | FILE_READ_ATTRIBUTES,
                                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)
                                        : INVALID_HANDLE_VALUE;
        FILE_DISPOSITION_INFO disposition{TRUE};
        const bool marked =
            pendingHandle != INVALID_HANDLE_VALUE &&
            ::SetFileInformationByHandle(pendingHandle, FileDispositionInfo, &disposition, sizeof(disposition)) != 0;
        check(marked, "hold native DeletePending lock while real compiler is paused");
        if (marked)
        {
            (void)std::filesystem::symlink_status(lock, pendingError);
            check(pendingError == std::errc::permission_denied, "DeletePending reproduces access denied");
            pg::abi::FailureReport noWait;
            check(!pg::abi::GuardCollection(request, noWait) && noWait.Count > 0,
                  "nonwaiting guard still rejects inaccessible lock");
        }
        Put(resume, "resume\n");
        const auto pendingDeadline = std::chrono::steady_clock::now() +
                                     (releaseBeforeDeadline ? std::chrono::seconds(1) : std::chrono::seconds(35));
        while (!finished && std::chrono::steady_clock::now() < pendingDeadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const bool completedWhileDenied = finished;
        check(marked && completedWhileDenied != releaseBeforeDeadline &&
                  !std::filesystem::exists(pg::abi::CompileReceiptPath(request, pausedUnits->front())),
              "pending lock blocks publication and persistent denial reaches bounded failure");
        if (pendingHandle != INVALID_HANDLE_VALUE)
        {
            check(::CloseHandle(pendingHandle) != 0, "release DeletePending lock handle");
        }
        pausedCompiler.join();
        if (releaseBeforeDeadline && pendingExit != 0)
        {
            pendingReport.WriteJson(std::cerr);
        }
        if (releaseBeforeDeadline)
        {
            const auto expectedCount = fullReport ? pg::abi::FailureReport::kCapacity : 0;
            check(pendingExit == 0 && pendingReport.Count == expectedCount && !pendingReport.Truncated &&
                      (!fullReport || pendingReport.Entries.front().Stage.View() == "retained_before_lock") &&
                      std::filesystem::is_regular_file(pg::abi::CompileReceiptPath(request, pausedUnits->front())),
                  "compiler publishes receipt after DeletePending lock disappears");
        }
        else
        {
            const auto native = pendingReport.Count == 1 ? pendingReport.Entries.front().Value.Native() : std::nullopt;
            check(completedWhileDenied && pendingExit != 0 && native && native->Source == pg::abi::kSystemSource &&
                      native->Value == ERROR_ACCESS_DENIED &&
                      pendingReport.Entries.front().Path.View() == lock.generic_string() &&
                      !std::filesystem::exists(pg::abi::CompileReceiptPath(request, pausedUnits->front())) &&
                      !std::filesystem::exists(request.OutputDirectory / "complete.json"),
                  "persistent denied lock retains native error and publishes no receipt or completion");
        }
        std::filesystem::remove(resume);
    };
    checkPendingLock(true, false);
    checkPendingLock(true, true);
    checkPendingLock(false, false);
    fixtureCommand.at(1) = "compile-fixture";
    writeCommand(fixtureCommand);
    check(pg::abi::RunObservedCompilerInvocation(request, fixtureUnits->front(), observation, report) == 0,
          "restore controlled compiler after lock regression");
#endif
    // The controlled child writes a fixture object; a standard archive still
    // contains exactly its bytes, exercising the production association gate.
    check(Archive(archiveRequest, object), "archive controlled process object");
    const auto fixtureSnapshot = pg::abi::SnapshotInputs(request, *fixtureUnits, observation, report);
    check(fixtureSnapshot && pg::abi::PublishCollection(request, profile, *fixtureSnapshot, report).has_value(),
          "controlled process publish");
    const auto invocations = Get(object.string() + ".invoked");
    const auto noProbe = pg::abi::GuardCollection(request, report);
    check(noProbe && *noProbe == pg::abi::GuardDisposition::kCurrent &&
              Get(object.string() + ".invoked") == invocations,
          "current Guard never launches compiler or probe");
    fixtureCommand.at(1) = "echo";
    writeCommand(fixtureCommand);
    const auto noOutput = pg::abi::LoadCompileUnits(request, report);
    pg::abi::FailureReport noOutputReport;
    check(noOutput &&
              pg::abi::RunObservedCompilerInvocation(request, noOutput->front(), observation, noOutputReport) != 0 &&
              !std::filesystem::exists(pg::abi::CompileReceiptPath(request, noOutput->front())),
          "exit zero without new object cannot receipt an old object");
    fixtureCommand.at(1) = "mutate-fixture";
    writeCommand(fixtureCommand);
    const auto mutating = pg::abi::LoadCompileUnits(request, report);
    pg::abi::FailureReport mutation;
    check(mutating && pg::abi::RunObservedCompilerInvocation(request, mutating->front(), observation, mutation) != 0 &&
              !std::filesystem::exists(pg::abi::CompileReceiptPath(request, mutating->front())) &&
              !std::filesystem::exists(request.OutputDirectory / "complete.json"),
          "source changed during real compile produces no receipt");
    fixtureCommand.at(1) = "mutate-header-fixture";
    writeCommand(fixtureCommand);
    const auto headerMutationUnit = pg::abi::LoadCompileUnits(request, report);
    pg::abi::FailureReport headerMutation;
    check(headerMutationUnit &&
              pg::abi::RunObservedCompilerInvocation(request, headerMutationUnit->front(), observation,
                                                     headerMutation) != 0 &&
              !std::filesystem::exists(pg::abi::CompileReceiptPath(request, headerMutationUnit->front())),
          "known header changed during execution cannot receive receipt");
    fixtureCommand.at(1) = "cancel-fixture";
    writeCommand(fixtureCommand);
    const auto cancelledUnit = pg::abi::LoadCompileUnits(request, report);
    const auto beforeCancel = Get(object.string() + ".invoked");
    std::stop_source cancellation;
    const std::jthread canceller(
        [&]
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (Get(object.string() + ".invoked") == beforeCancel && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            (void)cancellation.request_stop();
        });
    pg::abi::FailureReport cancelled;
    check(cancelledUnit &&
              pg::abi::RunObservedCompilerInvocation(request, cancelledUnit->front(), observation, cancelled,
                                                     cancellation.get_token()) != 0 &&
              !std::filesystem::exists(pg::abi::CompileReceiptPath(request, cancelledUnit->front())) &&
              !std::filesystem::exists(request.OutputDirectory / "complete.json"),
          "cancelled real child never publishes receipt or complete");
    // Frozen output links are refused before publication or cleanup touches inputs.
    const auto protectedInput = root / "protected-input";
    Put(protectedInput, "preserve input");
    const auto linkedOutput = request.OutputDirectory / "profile.json";
    std::filesystem::remove(linkedOutput);
    std::error_code hardlinkError;
    std::filesystem::create_hard_link(protectedInput, linkedOutput, hardlinkError);
    if (!hardlinkError)
    {
        pg::abi::FailureReport hardlink;
        check(!pg::abi::InvalidateCollection(request, hardlink) && Get(protectedInput) == "preserve input",
              "hardlinked output never cleans or rewrites input");
        std::filesystem::remove(linkedOutput);
    }
    else if (hardlinkError == std::errc::operation_not_supported ||
             hardlinkError == std::errc::function_not_supported || hardlinkError == std::errc::not_supported)
    {
        // macOS may build directly on the Windows SMB mount, which rejects hard links.
        // Windows/Linux run the security assertion; only this unsupported operation is skipped.
        std::cout << "hardlink unavailable on this filesystem: " << hardlinkError.message() << '\n';
    }
    else
    {
        std::cerr << "hardlink error: category=" << hardlinkError.category().name() << " code=" << hardlinkError.value()
                  << " message=" << hardlinkError.message() << '\n';
        check(false, "unexpected hardlink creation failure");
    }
    std::error_code symlinkError;
    std::filesystem::create_symlink(protectedInput, linkedOutput, symlinkError);
    if (!symlinkError)
    {
        pg::abi::FailureReport symlink;
        check(!pg::abi::InvalidateCollection(request, symlink) && Get(protectedInput) == "preserve input",
              "symlink output never cleans or rewrites input");
        std::filesystem::remove(linkedOutput);
    }
    else
    {
        std::cout << "symlink creation unavailable: " << symlinkError.message() << '\n';
    }
    return failures == 0 ? 0 : 1;
}
