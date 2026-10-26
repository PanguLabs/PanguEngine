#include "../../Tools/AbiProfile/Private/Collection.h"
#include "../../Tools/AbiProfile/Private/CollectionRequest.h"
#include "../../Tools/AbiProfile/Private/CollectionState.h"
#include "../../Tools/AbiProfile/Private/CompileCommands.h"
#include "../../Tools/AbiProfile/Private/Process.h"
#include "../../Tools/AbiProfile/Private/ToolIo.h"
#include "../../Tools/AbiProfile/Private/ToolReport.h"
#include "Result.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <iostream>
#include <span>
#include <stop_token>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - synchronous fault fixture.
int gFault = 0;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables) - observes real partial publication.
bool gPartialPublication = false;
pg::Status FaultPublish(const std::filesystem::path& from, const std::filesystem::path& to,
                        pg::abi::FailureReport& report)
{
    const bool secondComplete = to.filename() == "complete.json" && to.parent_path().filename() == "GroupB";
    if ((gFault == 1 && secondComplete) || (gFault == 2 && to.filename() == "group-complete.json"))
    {
        gPartialPublication = std::filesystem::exists(to.parent_path().parent_path() / "GroupA/complete.json") &&
                              !std::filesystem::exists(to.parent_path().parent_path() / "GroupA/group-complete.json");
        return std::unexpected(
            report.Add(pg::abi::ToolError::kWriteOutput, "injected_group_publish", to.generic_string()));
    }
    std::error_code error;
    std::filesystem::rename(from, to, error);
    return error ? pg::Status{std::unexpected(
                       report.Add(pg::abi::ToolError::kWriteOutput, "test_publish", to.generic_string(), error))}
                 : pg::Status{};
}
std::string Read(const std::filesystem::path& path)
{
    pg::abi::FailureReport report;
    const auto text = pg::abi::ReadFile(path, "test_read", report);
    return text ? *text : "";
}
bool Write(const std::filesystem::path& path, std::string_view text)
{
    pg::abi::FailureReport report;
    return pg::abi::WriteFile(path, text, report).has_value();
}
} // namespace
int main(int argc, char* const* argv)
{
    if (argc != 6)
    {
        return 2;
    }
    const auto args = std::span(argv, static_cast<std::size_t>(argc)).subspan(1);
    const auto sourceRoot = std::filesystem::absolute(args.front());
    const auto buildRoot = std::filesystem::absolute(args.subspan(1).front());
    const std::filesystem::path compiler(args.subspan(2).front());
    const std::filesystem::path manifest(args.subspan(3).front());
    const std::string configuration(args.back());
    const auto work = buildRoot / "Tests/AbiProfile/CollectionGroup";
    const auto outputRoot = buildRoot / "Abi" / configuration;
    std::filesystem::create_directories(work);
    pg::abi::FailureReport report;
    pg::abi::CollectionRequest baseline;
    baseline.SourceRoot = sourceRoot;
    baseline.BuildRoot = buildRoot;
    baseline.Compiler = compiler;
    baseline.Database = buildRoot / "compile_commands.json";
    baseline.ObjectManifest = manifest;
#ifdef _WIN32
    constexpr auto kPlatform = "windows";
    constexpr auto kStyle = "windows";
    baseline.CommandLineStyle = pg::abi::CommandLineStyle::kWindows;
#elifdef __APPLE__
    constexpr auto kPlatform = "macos";
    constexpr auto kStyle = "posix";
    baseline.CommandLineStyle = pg::abi::CommandLineStyle::kPosix;
#else
    constexpr auto kPlatform = "linux";
    constexpr auto kStyle = "posix";
    baseline.CommandLineStyle = pg::abi::CommandLineStyle::kPosix;
#endif
    const auto units = pg::abi::LoadCompileUnits(baseline, report);
    if (!units || units->empty())
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    auto graph = nlohmann::json{
        {"schema", 1},
        {"format", "cmake-actual-v1"},
        {"configuration", configuration},
        {"aliases", nlohmann::json::array()},
        {"targets", nlohmann::json::array()},
    };
    std::vector<pg::abi::CollectionRequest> requests;
    std::vector<std::vector<std::string>> commands;
    std::vector<std::filesystem::path> objects;
    for (const auto* name : {"GroupA", "GroupB"})
    {
        const std::string target(name);
        const auto source = work / (target + ".cpp");
        const auto object = work / (target + ".o");
        const auto output = outputRoot / target;
        const auto requestFile = work / (target + ".json");
        // These are dedicated test output directories, never engine artifacts.
        std::filesystem::remove_all(output);
        std::filesystem::remove(object);
        std::filesystem::create_directories(output);
        std::string sourceText = "#include <expected>\n#include \"";
        sourceText += target;
        sourceText += ".h\"\nstd::expected<int,int> ";
        sourceText += target;
        sourceText += "() { return kGroupValue; }\n";
        if (!Write(source, sourceText) || !Write(work / (target + ".h"), "constexpr int kGroupValue = 1;\n"))
        {
            return 1;
        }
        std::vector<std::string> command{compiler.generic_string()};
        const auto& baselineArguments = units->front().AbiArguments;
        for (std::size_t argument = 0; argument < baselineArguments.size(); ++argument)
        {
            if (baselineArguments.at(argument) == "-include" && argument + 1 < baselineArguments.size() &&
                std::filesystem::path(baselineArguments.at(argument + 1)).filename() == "inputs.h")
            {
                // The original Module signature is target-private. A new
                // independently collected STATIC fixture must not inherit it.
                ++argument;
                continue;
            }
            command.push_back(baselineArguments.at(argument));
        }
        command.insert(command.end(), {"-c", source.generic_string(), "-o", object.generic_string()});
        const auto database = nlohmann::json::array({
            {
                {"directory", std::filesystem::current_path().generic_string()},
                {"file", source.generic_string()},
                {"output", object.generic_string()},
                {"arguments", command},
            },
        });
        const auto request = nlohmann::json{
            {"schema", 2},
            {"source_root", sourceRoot.generic_string()},
            {"build_root", buildRoot.generic_string()},
            {"target_name", target},
            {"configuration", configuration},
            {"host_platform", kPlatform},
            {"target_platform", kPlatform},
            {"command_line_style", kStyle},
            {"compiler", compiler.generic_string()},
            {"database", (work / (target + "-commands.json")).generic_string()},
            {"object_manifest", (work / (target + "-objects.txt")).generic_string()},
            {"library", (work / (target + ".a")).generic_string()},
            {"output_directory", output.generic_string()},
            {"schema_file", (sourceRoot / "wiki/foundation/abi-profile-v1.schema.json").generic_string()},
            {"signature_header", (output / "inputs.h").generic_string()},
            {"link_graph", (work / "links.json").generic_string()},
            {"platform_context", nlohmann::json::object()},
            {"environment_baseline", nlohmann::json::object()},
        };
        if (!Write(work / (target + "-commands.json"), database.dump()) ||
            !Write(work / (target + "-objects.txt"), object.generic_string() + "\n") ||
            !Write(requestFile, request.dump()))
        {
            return 1;
        }
        graph.at("targets").push_back({
            {"name", target},
            {"kind", "static"},
            {"language", "cxx"},
            {"managed", false},
            {"source_visible", true},
            {"request_file", requestFile.generic_string()},
            {
                "links",
                nlohmann::json::array({{{"target", target == "GroupA" ? "GroupB" : "GroupA"}, {"scope", "private"}}}),
            },
        });
        commands.push_back(std::move(command));
        objects.push_back(object);
    }
    if (!Write(work / "links.json", graph.dump()))
    {
        return 1;
    }
    for (const auto* name : {"GroupA", "GroupB"})
    {
        const auto request = pg::abi::ReadCollectionRequest(work / (std::string(name) + ".json"), report);
        if (!request)
        {
            report.WriteJson(std::cerr);
            return 1;
        }
        requests.push_back(*request);
    }
    const auto build = [&](std::size_t index)
    {
        const auto& request = requests.at(index);
        if (!pg::abi::GuardCollection(request, report) ||
            pg::abi::RunCompilerInvocation(request, commands.at(index), report) != 0)
        {
            report.WriteJson(std::cerr);
            return false;
        }
        pg::abi::ProcessRequest archive;
#ifdef _WIN32
        archive.Program = compiler.parent_path() / "llvm-lib.exe";
        archive.Arguments = {"/nologo", "/OUT:" + request.Library.generic_string(), objects.at(index).generic_string()};
#else
        archive.Program = compiler.parent_path() / "llvm-ar";
        archive.Arguments = {"rcs", request.Library.generic_string(), objects.at(index).generic_string()};
#endif
        archive.Directory = work;
        const auto result = pg::abi::RunProcess(archive, report);
        if (!result || result->ExitCode != 0)
        {
            report.WriteJson(std::cerr);
            return false;
        }
        return true;
    };
    for (std::size_t index = 0; index < requests.size(); ++index)
    {
        if (!build(index))
        {
            return 1;
        }
    }
    if (!pg::abi::Collect(requests.front(), report))
    {
        std::cerr << "FAIL: cold SCC collection must attest both real archives without peer completion\n";
        report.WriteJson(std::cerr);
        return 1;
    }
    for (const auto& request : requests)
    {
        const auto state = pg::abi::GuardCollection(request, report);
        if (!state || *state != pg::abi::GuardDisposition::kCurrent)
        {
            std::cerr << "FAIL: every SCC member must have a stable cache\n";
            report.WriteJson(std::cerr);
            return 1;
        }
    }
    int failures = 0;
    const auto check = [&failures](bool passed, const char* name)
    {
        if (!passed)
        {
            std::cerr << "FAIL: " << name << '\n';
            ++failures;
        }
    };
    const auto sealPath = requests.front().OutputDirectory / "group-complete.json";
    const auto seal = Read(sealPath);
    const auto completeA = Read(requests.front().OutputDirectory / "complete.json");
    const auto completeB = Read(requests.back().OutputDirectory / "complete.json");
    check(pg::abi::GuardCollection(requests.back(), report).has_value() && Read(sealPath) == seal &&
              Read(requests.front().OutputDirectory / "complete.json") == completeA &&
              Read(requests.back().OutputDirectory / "complete.json") == completeB,
          "cache preserves exact sealed generation");

    std::vector<pg::abi::CollectedProfile> profiles;
    std::vector<pg::abi::InputSnapshot> snapshots;
    for (const auto& request : requests)
    {
        const auto memberUnits = pg::abi::LoadCompileUnits(request, report);
        if (!memberUnits)
        {
            return 1;
        }
        const auto observation = pg::abi::InspectCompileInputs(request, *memberUnits, report);
        if (!observation)
        {
            report.WriteJson(std::cerr);
            return 1;
        }
        const auto snapshot = pg::abi::SnapshotInputs(request, *memberUnits, *observation, report);
        if (!snapshot)
        {
            return 1;
        }
        const auto evidence = nlohmann::json::parse(Read(request.OutputDirectory / "evidence.json"));
        pg::abi::CollectedProfile profile;
        profile.Profile = nlohmann::json::parse(Read(request.OutputDirectory / "profile.json"));
        profile.Evidence = evidence.at("platform");
        for (const auto& file : evidence.at("snapshot").at("files"))
        {
            if (file.at("role") == "collection")
            {
                profile.ObservedInputs.emplace_back(file.at("path").get<std::string>());
            }
        }
        profiles.push_back(std::move(profile));
        snapshots.push_back(*snapshot);
    }
    const auto revoked = [&]
    {
        return !std::filesystem::exists(sealPath) &&
               !std::filesystem::exists(requests.front().OutputDirectory / "complete.json") &&
               !std::filesystem::exists(requests.back().OutputDirectory / "complete.json");
    };
    const auto publish = [&]
    {
        pg::abi::FailureReport publication;
        const auto status = pg::abi::PublishCollectionGroup(requests, profiles, snapshots, publication);
        if (!status)
        {
            publication.WriteJson(std::cerr);
        }
        return status.has_value();
    };
    for (const int fault : {1, 2})
    {
        gFault = fault;
        gPartialPublication = false;
        pg::abi::FailureReport failed;
        pg::abi::CollectionIo io;
        io.Publish = &FaultPublish;
        check(!pg::abi::PublishCollectionGroup(requests, profiles, snapshots, failed, io) && gPartialPublication &&
                  revoked(),
              "partial completion or final seal failure revokes whole group");
        gFault = 0;
        check(publish(), "group can recover using unchanged genuine receipts");
    }
    auto incompatible = profiles;
    incompatible.back().Profile.at("language").at("rtti") = "off";
    pg::abi::FailureReport mismatch;
    check(!pg::abi::PublishCollectionGroup(requests, incompatible, snapshots, mismatch) && revoked() &&
              mismatch.Count > 0 && mismatch.Entries.front().JsonPointer.View() == "/language/rtti",
          "incompatible peer reports exact field and publishes no completion");
    check(publish(), "restore compatible group");

    {
        pg::abi::FailureReport ownerReport;
        auto owner = pg::abi::BeginCollectionTransaction(requests, ownerReport);
        pg::abi::FailureReport conflict;
        const auto competing = pg::abi::BeginCollectionTransaction(requests, conflict);
        check(owner && !competing && !(*owner)->Publish(profiles, snapshots),
              "concurrent group writer fails closed without waiting cyclically");
    }
    check(revoked() && publish(), "writer conflict invalidates entire group and releases all locks");

    // A killed process between member completion and seal leaves no valid group.
    std::filesystem::remove(sealPath);
    const auto interrupted = pg::abi::GuardCollection(requests.back(), report);
    check(interrupted && *interrupted == pg::abi::GuardDisposition::kBuildRequired && revoked(),
          "unsealed partial generation cannot become current");
    check(publish(), "recover interrupted publication");
    const auto peerUnits = pg::abi::LoadCompileUnits(requests.back(), report);
    if (!peerUnits)
    {
        return 1;
    }
    const auto receiptPath = pg::abi::CompileReceiptPath(requests.back(), peerUnits->front());
    const auto receipt = Read(receiptPath);
    check(Write(receiptPath, "{}"), "tamper peer receipt");
    pg::abi::FailureReport tampered;
    check(!pg::abi::GuardCollection(requests.front(), tampered) && revoked(), "Guard A checks B receipt provenance");
    check(Write(receiptPath, receipt) && publish(), "restore receipt and publish");
    const auto peerSource = work / "GroupB.cpp";
    const auto peerSourceBytes = Read(peerSource);
    const auto peerArchiveBytes = Read(requests.back().Library);
    check(Write(peerSource, peerSourceBytes + "\nint unarchived_peer() { return 2; }\n") &&
              pg::abi::RunCompilerInvocation(requests.back(), commands.back(), report) == 0 &&
              Read(requests.back().Library) == peerArchiveBytes,
          "peer implementation really recompiles while archive remains unchanged");
    pg::abi::FailureReport unarchivedPeer;
    check(!pg::abi::Collect(requests.front(), unarchivedPeer) && revoked(),
          "SCC rejects fresh peer receipt bound to stale archive and revokes all completion state");
    check(Write(peerSource, peerSourceBytes) && build(0) && build(1) && pg::abi::Collect(requests.front(), report),
          "SCC recovers only after genuine compilation and archiving");
    for (const auto& changed : {work / "GroupB.cpp", work / "GroupB.h"})
    {
        const auto old = Read(changed);
        check(Write(changed, old + "\n// changed peer input\n"), "change peer input");
        pg::abi::FailureReport drift;
        const auto state = pg::abi::GuardCollection(requests.front(), drift);
        check(state && *state == pg::abi::GuardDisposition::kBuildRequired && revoked(),
              "Guard A detects B source/header drift and revokes all");
        check(Write(changed, old), "restore peer input bytes");
        // The invalidated generation is not attested merely because the
        // source bytes were restored. Rebuild to obtain fresh compiler receipts.
        bool rebuilt = true;
        for (std::size_t member = 0; member < requests.size(); ++member)
        {
            rebuilt = build(member) && rebuilt;
        }
        check(rebuilt && pg::abi::Collect(requests.front(), report),
              "restore peer input with genuine compiler receipts");
    }
    // Cancellation uses the real observed-compiler boundary, never a fake receipt.
    const auto observation = pg::abi::InspectCompileInputs(requests.back(), *peerUnits, report);
    if (!observation)
    {
        return 1;
    }
    std::stop_source cancelled;
    cancelled.request_stop();
    pg::abi::FailureReport cancellation;
    check(pg::abi::RunObservedCompilerInvocation(requests.back(), peerUnits->front(), *observation, cancellation,
                                                 cancelled.get_token()) != 0 &&
              revoked(),
          "cancelled peer compile revokes all old completion state");
    // Actual compiler option disagreement must fail at the same exact v1 field.
#ifdef _WIN32
    // MSVC STL's RTTI-off defaults are outside the backend contract. Signedness
    // produces two supported but incompatible profiles without changing policy.
    commands.back().emplace_back("-funsigned-char");
    constexpr auto kMismatchField = "/language/abi_options/2";
#else
    commands.back().emplace_back("-fno-rtti");
    constexpr auto kMismatchField = "/language/rtti";
#endif
    auto peerDatabase = nlohmann::json::parse(Read(requests.back().Database));
    peerDatabase.at(0).at("arguments") = commands.back();
    check(Write(requests.back().Database, peerDatabase.dump()) && build(1), "compile genuinely incompatible peer");
    pg::abi::FailureReport actualMismatch;
    check(!pg::abi::Collect(requests.back(), actualMismatch) && revoked() && actualMismatch.Count > 0 &&
              actualMismatch.Entries.front().JsonPointer.View() == kMismatchField,
          "real incompatible peer rejected by production Collect with field diagnostic");
    if (actualMismatch.Count > 0 && actualMismatch.Entries.front().JsonPointer.View() != kMismatchField)
    {
        actualMismatch.WriteJson(std::cerr);
    }
    return failures == 0 ? 0 : 1;
}
