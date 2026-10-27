#include "../../BuildTools/AbiProfile/Private/CollectionRequest.h"
#include "../../BuildTools/AbiProfile/Private/CollectionState.h"
#include "../../BuildTools/AbiProfile/Private/ToolIo.h"
#include "../../BuildTools/AbiProfile/Private/ToolReport.h"
#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winerror.h>
#endif

#ifdef __APPLE__
#include <chrono>
#include <unistd.h>
#endif

namespace
{
using Json = nlohmann::json;

[[nodiscard]] Json Request(const std::filesystem::path& root)
{
    const auto build = root / "build";
    const auto output = build / "Abi/Debug/Example";
    return Json{
        {"schema", 2},
        {"source_root", (root / "source").generic_string()},
        {"build_root", build.generic_string()},
        {"target_name", "Example"},
        {"configuration", "Debug"},
        {"host_platform", "windows"},
        {"target_platform", "windows"},
        {"command_line_style", "windows"},
        {"compiler", (root / "compiler.exe").generic_string()},
        {"database", (build / "compile_commands.json").generic_string()},
        {"object_manifest", (build / "objects.txt").generic_string()},
        {"library", (build / "Example.lib").generic_string()},
        {"output_directory", output.generic_string()},
        {"schema_file", (root / "schema.json").generic_string()},
        {"signature_header", (output / "signature.h").generic_string()},
        {"link_graph", (build / "links.json").generic_string()},
        {"platform_context", Json{{"sdk_hint", "declaration only"}}},
        {"environment_baseline", Json{{"INCLUDE", "toolchain headers"}}},
    };
}

[[nodiscard]] bool Check(const std::filesystem::path& path, std::string_view contents, bool accepted,
                         std::string_view label)
{
    pg::abi::FailureReport writeReport;
    if (!pg::abi::WriteFile(path, contents, writeReport))
    {
        std::cerr << "fixture write failed: " << label << '\n';
        return false;
    }
    pg::abi::FailureReport report;
    const auto result = pg::abi::ReadCollectionRequest(path, report);
    pg::abi::FailureReport readReport;
    const auto after = pg::abi::ReadFile(path, "verify_request", readReport);
    const bool passed =
        result.has_value() == accepted && after && *after == contents && (accepted || report.Count != 0);
    if (!passed)
    {
        std::cerr << "request check failed: " << label << " (accepted=" << result.has_value() << ")\n";
        report.WriteJson(std::cerr);
    }
    return passed;
}

// Removing the work reservation must allow these lifecycle writes and fail the
// byte-preservation assertions. No compiler process or synthetic backend is used.
[[nodiscard]] bool CheckLifecycle(const std::filesystem::path& root)
{
    std::error_code error;
    std::filesystem::create_directories(root / "source", error);
    std::filesystem::create_directories(root / "build/Abi/Debug/Example/work", error);
    const auto path = root / "request.json";
    pg::abi::FailureReport report;
    if (error || !pg::abi::WriteFile(path, Request(root).dump(), report))
    {
        return false;
    }
    auto parsed = pg::abi::ReadCollectionRequest(path, report);
    if (!parsed)
    {
        return false;
    }
    auto request = *parsed;
    request.EnvironmentBaseline.clear();
    const auto source = root / "source/unit.cpp";
    const auto object = root / "build/unit.o";
    const auto work = request.OutputDirectory / "work";
    const auto prepare = [&](const std::filesystem::path& unitSource, const std::filesystem::path& unitObject,
                             const std::filesystem::path& response)
    {
        std::vector<std::string> arguments{
            request.Compiler.generic_string(), "-c", unitSource.generic_string(), "-o", unitObject.generic_string(),
        };
        if (!response.empty())
        {
            arguments = {request.Compiler.generic_string(), "@" + response.generic_string()};
            const auto text = "-c \"" + unitSource.generic_string() + "\" -o \"" + unitObject.generic_string() + "\"";
            if (!pg::abi::WriteFile(response, text, report))
            {
                return false;
            }
        }
        const auto database = Json::array({
            {
                {"directory", root.generic_string()},
                {"file", unitSource.generic_string()},
                {"output", unitObject.generic_string()},
                {"arguments", arguments},
            },
        });
        return pg::abi::WriteFile(request.Database, database.dump(), report) &&
               pg::abi::WriteFile(request.ObjectManifest, unitObject.generic_string() + '\n', report) &&
               pg::abi::WriteFile(unitSource, "int value;\n", report);
    };
    if (!prepare(source, object, {}) || !pg::abi::InvalidateCollection(request, report))
    {
        std::cerr << "valid typed lifecycle fixture failed\n";
        report.WriteJson(std::cerr);
        return false;
    }
    bool passed = true;
    for (const auto* role : {"request", "source_root", "source", "object", "response"})
    {
        request = *parsed;
        request.EnvironmentBaseline.clear();
        const auto protectedPath =
            work / (std::string_view(role) == "request" ? "unit-0.cpp" : std::string(role) + ".cpp");
        if (std::string_view(role) == "request")
        {
            request.RequestFile = protectedPath;
        }
        if (std::string_view(role) == "source_root")
        {
            request.SourceRoot = work;
        }
        if (!prepare(std::string_view(role) == "source" ? protectedPath : source,
                     std::string_view(role) == "object" ? protectedPath : object,
                     std::string_view(role) == "response" ? protectedPath : std::filesystem::path{}) ||
            !pg::abi::WriteFile(protectedPath,
                                std::string_view(role) == "response"
                                    ? "-c \"" + source.generic_string() + "\" -o \"" + object.generic_string() + "\""
                                    : "preserve typed input bytes",
                                report))
        {
            return false;
        }
        pg::abi::FailureReport beforeReport;
        const auto before = pg::abi::ReadFile(protectedPath, "before", beforeReport);
        const auto evidence = request.OutputDirectory / "evidence.json";
        if (!pg::abi::WriteFile(evidence, "preserve previous evidence", beforeReport))
        {
            return false;
        }
        pg::abi::FailureReport lifecycleReport;
        const auto invalidated = pg::abi::InvalidateCollection(request, lifecycleReport);
        const auto after = pg::abi::ReadFile(protectedPath, "after", beforeReport);
        const auto afterEvidence = pg::abi::ReadFile(evidence, "after_evidence", beforeReport);
        if (invalidated || lifecycleReport.Count == 0 || !before || !after || *before != *after || !afterEvidence ||
            *afterEvidence != "preserve previous evidence")
        {
            std::cerr << "typed lifecycle accepted reserved work input: " << role << '\n';
            passed = false;
        }
    }
    return passed;
}

[[nodiscard]] bool CheckWorkAliases(const std::filesystem::path& root)
{
    std::error_code error;
    const auto work = root / "build/Abi/Debug/Example/work";
    std::filesystem::create_directories(work, error);
    const auto requestFile = root / "request.json";
    const auto original = Request(root);
    bool passed = !error && Check(requestFile, original.dump(), true, "work alias fixture");
    pg::abi::FailureReport report;
    const auto request = pg::abi::ReadCollectionRequest(requestFile, report);
    if (!request)
    {
        return false;
    }
    for (const auto& input :
         {work / "header.h", work / "../work/response.rsp", work, root / "build/Abi/Debug/Example/working/header.h"})
    {
        pg::abi::FailureReport inputReport;
        const std::array inputs{input};
        const bool expected = input.parent_path().filename() == "working";
        const auto checked = pg::abi::CheckCollectionInputPaths(*request, inputs, inputReport);
        if (checked.has_value() != expected)
        {
            std::cerr << "typed input work boundary failed: " << input << '\n';
            passed = false;
        }
    }
#ifdef _WIN32
    const std::array caseAlias{root / "build/Abi/Debug/Example/WORK/unit-0.cpp"};
    pg::abi::FailureReport caseReport;
    passed &= !pg::abi::CheckCollectionInputPaths(*request, caseAlias, caseReport);
#endif
    const auto output = work / "unit-0.cpp";
    const auto alias = root / "outside.cpp";
    if (!pg::abi::WriteFile(output, "preserve linked source", report))
    {
        return false;
    }
    std::filesystem::create_hard_link(output, alias, error);
    if (error)
    {
        std::cerr << "work hard link fixture failed: " << error.message() << '\n';
        return false;
    }
    auto linked = original;
    linked.at("schema_file") = alias.generic_string();
    passed &= Check(requestFile, linked.dump(), false, "work file hard link aliases external input");
    const auto after = pg::abi::ReadFile(alias, "work_alias_bytes", report);
    passed &= after && *after == "preserve linked source";
    std::filesystem::remove(alias, error);
    std::filesystem::remove(output, error);

    const auto target = root / "external";
    std::filesystem::create_directory(target, error);
    const auto link = work / "nested";
    std::filesystem::create_directory_symlink(target, link, error);
    bool permissionDenied = error == std::errc::operation_not_permitted || error == std::errc::permission_denied;
#ifdef _WIN32
    permissionDenied |= error.value() == ERROR_PRIVILEGE_NOT_HELD;
#endif
    if (permissionDenied)
    {
        std::cerr << "SKIP: directory symlink fixtures require host permission\n";
        return passed;
    }
    if (error)
    {
        std::cerr << "work symlink fixture failed: " << error.message() << '\n';
        return false;
    }
    linked = original;
    linked.at("schema_file") = (target / "unit-0.cpp").generic_string();
    passed &= Check(requestFile, linked.dump(), false, "nested work symlink targets external input");
    std::filesystem::remove(link, error);

    const auto outsideLink = root / "work-alias";
    std::filesystem::create_directory_symlink(work, outsideLink, error);
    if (error)
    {
        return false;
    }
    linked = original;
    linked.at("schema_file") = (outsideLink / "unit-0.cpp").generic_string();
    passed &= Check(requestFile, linked.dump(), false, "external directory symlink aliases work input");
    std::filesystem::remove(outsideLink, error);
    return passed;
}

[[nodiscard]] bool CheckPrivateWork(const std::filesystem::path& root)
{
    const auto work = root / "build/Abi/Debug/Example/work";
    std::error_code error;
    std::filesystem::create_directories(work, error);
    const auto original = Request(root);
    const auto requestFile = root / "request.json";
    if (error || !Check(requestFile, original.dump(), true, "private work fixture"))
    {
        return false;
    }
    pg::abi::FailureReport readReport;
    const auto request = pg::abi::ReadCollectionRequest(requestFile, readReport);
    if (!request)
    {
        return false;
    }
    std::atomic<bool> stop{false};
    std::atomic<bool> producerFailed{false};
    std::atomic<unsigned> completed{};
    std::jthread producer(
        [&]
        {
            for (unsigned generation = 0; !stop; ++generation)
            {
                const auto owned = work / (".observation-race-" + std::to_string(generation));
                std::error_code producerError;
                if (!std::filesystem::create_directory(owned, producerError))
                {
                    producerFailed = true;
                    return;
                }
                for (unsigned index = 0; index != 8; ++index)
                {
                    const auto child = owned / ("child-" + std::to_string(index));
                    if (!std::filesystem::create_directory(child, producerError))
                    {
                        producerFailed = true;
                        return;
                    }
                    std::ofstream file(child / "probe.cpp", std::ios::binary);
                    file << "int probe;\n";
                }
                // Only this producer's atomically created direct child is removed.
                (void)std::filesystem::remove_all(owned, producerError);
                if (producerError)
                {
                    producerFailed = true;
                    return;
                }
                ++completed;
            }
        });
    unsigned failures{};
    for (unsigned attempt = 0; attempt != 400; ++attempt)
    {
        pg::abi::FailureReport report;
        if (!pg::abi::CheckCollectionInputPaths(*request, {}, report))
        {
            if (failures == 0)
            {
                std::cerr << "private work cleanup raced input guard:\n";
                report.WriteJson(std::cerr);
            }
            ++failures;
        }
    }
    stop = true;
    producer.join();
    std::cerr << "private work guard checks=400 failures=" << failures << " cleanup_cycles=" << completed << '\n';
    bool passed = failures == 0 && !producerFailed && completed != 0;

    const auto privateRoot = work / ".observation-abandoned";
    const auto target = root / "external";
    std::filesystem::create_directories(target, error);
    std::filesystem::create_directory(privateRoot, error);
    const auto link = privateRoot / "nested";
    std::filesystem::create_directory_symlink(target, link, error);
    bool permissionDenied = error == std::errc::operation_not_permitted || error == std::errc::permission_denied;
#ifdef _WIN32
    permissionDenied |= error.value() == ERROR_PRIVILEGE_NOT_HELD;
#endif
    if (permissionDenied)
    {
        std::cerr << "SKIP: private directory symlink fixtures require host permission\n";
        std::filesystem::remove(privateRoot, error);
        return passed;
    }
    if (error)
    {
        return false;
    }
    passed &= Check(requestFile, original.dump(), true, "never reused private tree does not constrain other probes");
    auto linked = original;
    linked.at("schema_file") = (link / "schema.json").generic_string();
    passed &= Check(requestFile, linked.dump(), false, "logical private path remains reserved through symlink");
    const std::array inputs{link / "source.cpp"};
    pg::abi::FailureReport logicalReport;
    passed &= !pg::abi::CheckCollectionInputPaths(*request, inputs, logicalReport);
    std::filesystem::remove(link, error);
    std::filesystem::remove(privateRoot, error);
    std::filesystem::create_directory_symlink(target, privateRoot, error);
    if (error)
    {
        return false;
    }
    passed &= Check(requestFile, original.dump(), false, "private root symlink is still rejected before skipping");
    std::filesystem::remove(privateRoot, error);
    return passed;
}

[[nodiscard]] bool Run(const std::filesystem::path& root)
{
    std::error_code error;
    std::filesystem::create_directories(root / "build/Abi/Debug/Example", error);
    if (error)
    {
        return false;
    }
    // Previous runs may leave this test's deliberate output hard-link fixture.
    // Clear only the two known fixture files before checking a cold request.
    std::filesystem::remove(root / "schema-hardlink.json", error);
    if (error)
    {
        return false;
    }
    std::filesystem::remove(root / "build/Abi/Debug/Example/fingerprint.sha256", error);
    if (error)
    {
        return false;
    }
    const auto path = root / "request.json";
    const auto original = Request(root);
    bool passed = Check(path, original.dump(), true, "cold build is valid");
    pg::abi::FailureReport typedReport;
    const auto typed = pg::abi::ReadCollectionRequest(path, typedReport);
    passed &= typed && typed->Compiler == root / "compiler.exe" && typed->TargetName == "Example" &&
              typed->ObjectManifest == root / "build/objects.txt" && typed->Library == root / "build/Example.lib" &&
              typed->HostPlatform == pg::abi::Platform::kWindows &&
              typed->CommandLineStyle == pg::abi::CommandLineStyle::kWindows &&
              typed->PlatformContext.at("sdk_hint") == "declaration only" &&
              typed->EnvironmentBaseline.at("INCLUDE") == "toolchain headers";
    for (const auto version : {0, 1, 3})
    {
        auto request = original;
        request.at("schema") = version;
        passed &= Check(path, request.dump(), false, "requires schema 2");
    }
    for (const auto& [key, value] : original.items())
    {
        (void)value;
        auto request = original;
        request.erase(key);
        passed &= Check(path, request.dump(), false, std::string("missing ") + key);
    }
    for (const auto& [key, value] : original.items())
    {
        (void)value;
        auto request = original;
        request.at(key) = Json::array();
        passed &= Check(path, request.dump(), false, std::string("wrong type ") + key);
    }
    auto duplicate = original.dump();
    duplicate.insert(1, "\"schema\":2,");
    passed &= Check(path, duplicate, false, "duplicate key");
    auto request = original;
    request.emplace("profile", Json{{"schema", 1}});
    passed &= Check(path, request.dump(), false, "unexpected Profile leaf data");
    request = original;
    request.at("source_root") = (root / "build/Abi/Debug/Example").generic_string();
    passed &= Check(path, request.dump(), false, "source directory aliases output");
    request = original;
    request.at("environment_baseline").at("INCLUDE") = false;
    passed &= Check(path, request.dump(), false, "environment value type");
    request = original;
    request.at("environment_baseline") = Json{{"INCLUDE", nullptr}, {"LIB", ""}};
    passed &= Check(path, request.dump(), true, "environment distinguishes unset and empty");
    for (const auto* name : {
             ".invalid",
             "archive-binding.json",
             "group-complete.json",
             "receipts/request.json",
             ".collection-lock/request.json",
             "fixture.compiling/request.json",
             "work/unit-0.cpp",
             "work/runtime-witness.obj",
             "work/runtime-witness.exe",
             "work/runtime-witness",
             "work/headers.txt",
         })
    {
        const auto reservedPath = root / "build/Abi/Debug/Example" / name;
        std::filesystem::create_directories(reservedPath.parent_path());
        passed &= Check(reservedPath, original.dump(), false, "request in reserved state path");
        std::filesystem::remove(reservedPath);
        request = original;
        request.at("signature_header") = reservedPath.generic_string();
        passed &= Check(path, request.dump(), false, "signature in reserved state path");
    }
    for (const auto* key :
         {"source_root", "compiler", "database", "object_manifest", "library", "schema_file", "link_graph"})
    {
        request = original;
        request.at(key) = (root / "build/Abi/Debug/Example/work/input").generic_string();
        passed &= Check(path, request.dump(), false, std::string("work input ") + key);
    }
    request = original;
    request.at("object_manifest") = (root / "build/Abi/Debug/Example/objects.txt").generic_string();
    passed &= Check(root / "build/Abi/Debug/Example/request.json", request.dump(), true,
                    "ordinary request and manifest remain valid beside reserved work");
    request = original;
    request.at("platform_context").emplace("nested", Json{{"a", 1}});
    auto nestedDuplicate = request.dump();
    const auto nested = nestedDuplicate.find("\"a\":1");
    nestedDuplicate.insert(nested, "\"a\":2,");
    passed &= Check(path, nestedDuplicate, false, "nested duplicate key");
    for (const auto* key : {"compiler", "database", "object_manifest", "library", "schema_file", "link_graph"})
    {
        request = original;
        request.at(key) = (root / "build/Abi/Debug/Example/canonical.json").generic_string();
        passed &= Check(path, request.dump(), false, std::string("output aliases ") + key);
    }
    request = original;
    request.at("signature_header") = path.generic_string();
    passed &= Check(path, request.dump(), false, "signature aliases request");
    request = original;
    request.at("schema_file") = (root / "build/Abi/Debug/Example/../Example/profile.json").generic_string();
    passed &= Check(path, request.dump(), false, "normalized output alias");
    request = original;
    request.at("database") = (root / "build/Abi/Debug/Example").generic_string();
    passed &= Check(path, request.dump(), false, "output directory is input");
    request = original;
    request.at("target_name") = "../Example";
    passed &= Check(path, request.dump(), false, "target path traversal");
    request = original;
    request.at("host_platform") = "android";
    passed &= Check(path, request.dump(), false, "unsupported host");
    request = original;
    request.at("command_line_style") = "posix";
    passed &= Check(path, request.dump(), false, "host quoting mismatch");
    request = original;
    request.at("signature_header") = (root / "build/Abi/Debug/Example/profile.json").generic_string();
    passed &= Check(path, request.dump(), false, "signature aliases stable output");
    request = original;
    request.at("signature_header") = (root / "build/Abi/Debug/Example/profile.json/generated.h").generic_string();
    passed &= Check(path, request.dump(), false, "signature below stable output ancestor");
    request = original;
    request.at("library") = "relative.lib";
    passed &= Check(path, request.dump(), false, "relative path is ambiguous");
    request = original;
    request.at("compiler") = std::string("compiler\0path", 13);
    passed &= Check(path, request.dump(), false, "embedded NUL path");
    passed &= Check(path, "[]", false, "request must be object");
    passed &= Check(path, "{", false, "malformed JSON");
#ifdef _WIN32
    request = original;
    request.at("schema_file") = (root / "build/Abi/Debug/Example/CANONICAL.JSON").generic_string();
    passed &= Check(path, request.dump(), false, "absent case-folded alias");
    request = original;
    request.at("schema_file") = (root / "build/Abi/Debug/Example/canonical.json.").generic_string();
    passed &= Check(path, request.dump(), false, "Windows trailing dot alias");
#endif

    // Hard links must be checked by identity, not only by normalized spelling.
    const auto output = root / "build/Abi/Debug/Example/fingerprint.sha256";
    pg::abi::FailureReport outputReport;
    if (!pg::abi::WriteFile(output, "keep output input bytes", outputReport))
    {
        return false;
    }
    const auto alias = root / "schema-hardlink.json";
    std::filesystem::remove(alias, error);
    error.clear();
    std::filesystem::create_hard_link(output, alias, error);
    if (error)
    {
        std::cerr << "hard link fixture failed: " << error.message() << '\n';
        return false;
    }
    request = original;
    request.at("schema_file") = alias.generic_string();
    passed &= Check(path, request.dump(), false, "hard link output alias");
    const auto after = pg::abi::ReadFile(alias, "verify_alias", outputReport);
    passed &= after && *after == "keep output input bytes";
    passed &= CheckLifecycle(root / "Lifecycle");
    passed &= CheckWorkAliases(root / "Aliases");
    passed &= CheckPrivateWork(root / "PrivateWork");
    return passed;
}
} // namespace

int main(int argc, char* const* argv)
{
    if (argc != 2)
    {
        return 2;
    }
#ifdef __APPLE__
    (void)argv;
    // Use a private native-filesystem fixture for the hard-link identity checks,
    // independently of where the source tree is mounted.
    std::error_code error;
    const auto temporary = std::filesystem::temp_directory_path(error);
    if (error)
    {
        std::cerr << "native temporary directory failed: " << error.message() << '\n';
        return 1;
    }
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root =
        temporary / ("PanguCollectionRequest-" + std::to_string(::getpid()) + '-' + std::to_string(nonce));
    if (!std::filesystem::create_directory(root, error))
    {
        std::cerr << "native unique fixture directory failed\n";
        return 1;
    }
    // Run returns a status on every path; exceptions are disabled for this target.
    const bool passed = Run(root);
    (void)std::filesystem::remove_all(root, error);
    if (error)
    {
        std::cerr << "native fixture cleanup failed: " << error.message() << '\n';
        return 1;
    }
    return passed ? 0 : 1;
#else
    // The argument count above guards this command-line access.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    return Run(std::filesystem::absolute(argv[1]) / "CollectionRequest") ? 0 : 1;
#endif
}
