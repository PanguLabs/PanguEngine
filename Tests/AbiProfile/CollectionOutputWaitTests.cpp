#include "../../BuildTools/AbiProfile/Private/CollectionRequest.h"
#include "../../BuildTools/AbiProfile/Private/DependencyGraph.h"
#include "../../BuildTools/AbiProfile/Private/ToolReport.h"

#include <Result.h>
#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>

#define NOMINMAX
#include <Windows.h> // IWYU pragma: keep - native DeletePending regression.
#include <fileapi.h>
#include <handleapi.h>
#include <minwinbase.h>
#include <minwindef.h>
#include <winbase.h>
#include <winerror.h>
#include <winnt.h>

namespace
{
void Put(const std::filesystem::path& path, std::string_view text) { std::ofstream(path) << text; }

class PendingFile final
{
public:
    explicit PendingFile(const std::filesystem::path& path)
        : Handle(Open(path))
    {
        FILE_DISPOSITION_INFO disposition{TRUE};
        Marked = Handle != INVALID_HANDLE_VALUE &&
                 ::SetFileInformationByHandle(Handle, FileDispositionInfo, &disposition, sizeof(disposition)) != 0;
    }
    ~PendingFile() { Release(); }
    PendingFile(const PendingFile&) = delete;
    PendingFile& operator=(const PendingFile&) = delete;
    PendingFile(PendingFile&&) = delete;
    PendingFile& operator=(PendingFile&&) = delete;
    [[nodiscard]] bool IsMarked() const noexcept { return Marked; }
    void Release() noexcept
    {
        if (Handle != INVALID_HANDLE_VALUE)
        {
            (void)::CloseHandle(Handle);
            Handle = INVALID_HANDLE_VALUE;
        }
    }

private:
    static HANDLE Open(const std::filesystem::path& path)
    {
        Put(path, "pending output");
        return ::CreateFileW(path.c_str(), DELETE | FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0,
                             nullptr);
    }
    HANDLE Handle{INVALID_HANDLE_VALUE};
    bool Marked{};
};

bool Denied(const pg::Status& status)
{
    const auto native = !status ? status.error().Native() : std::nullopt;
    return native && native->Source == pg::abi::kSystemSource && native->Value == ERROR_ACCESS_DENIED;
}
} // namespace

int main(int argc, char* const* argv)
{
    if (argc != 2)
    {
        return 2;
    }
    const auto root = std::filesystem::absolute(std::span(argv, static_cast<std::size_t>(argc)).back()) / "OutputWait" /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto output = root / "Abi/Debug/Fixture";
    std::filesystem::create_directories(output);
    const auto path = root / "request.json";
    const nlohmann::json document{
        {"schema", 2},
        {"source_root", root.generic_string()},
        {"build_root", root.generic_string()},
        {"target_name", "Fixture"},
        {"configuration", "Debug"},
        {"host_platform", "windows"},
        {"target_platform", "windows"},
        {"command_line_style", "windows"},
        {"compiler", (root / "compiler.exe").generic_string()},
        {"database", (root / "database.json").generic_string()},
        {"object_manifest", (root / "objects.txt").generic_string()},
        {"library", (root / "fixture.a").generic_string()},
        {"output_directory", output.generic_string()},
        {"schema_file", (root / "schema.json").generic_string()},
        {"signature_header", (output / "inputs.h").generic_string()},
        {"link_graph", (root / "graph.json").generic_string()},
        {"platform_context", nlohmann::json::object()},
        {"environment_baseline", nlohmann::json::object()},
    };
    for (const auto* name : {"compiler.exe", "database.json", "objects.txt", "schema.json", "graph.json"})
    {
        Put(root / name, "{}");
    }
    Put(path, document.dump());
    pg::abi::FailureReport setup;
    const auto request = pg::abi::ReadCollectionRequest(path, setup);
    if (!request)
    {
        setup.WriteJson(std::cerr);
        return 1;
    }
    pg::abi::DependencyGraph graph;
    pg::abi::DependencyTarget target;
    target.Name = "Fixture";
    target.Kind = pg::abi::DependencyKind::kStatic;
    target.RequestFile = path;
    graph.Targets.emplace("Fixture", target);
    pg::abi::DependencyPlan plan;
    plan.TechnicalCycles = {{"Fixture"}};
    const std::array inputs{root / "database.json"};
    int failures = 0;
    const auto check = [&](bool passed, std::string_view name)
    {
        if (!passed)
        {
            ++failures;
            std::cerr << "FAIL: " << name << '\n';
        }
    };
    const auto operation = [&](int kind, pg::abi::CollectionOutputWait wait,
                               pg::abi::FailureReport& report) -> pg::Status
    {
        if (kind == 0)
        {
            const auto result = pg::abi::ReadCollectionRequest(path, report, wait);
            return result ? pg::Status{} : std::unexpected(result.error());
        }
        if (kind == 1)
        {
            return pg::abi::CheckCollectionInputPaths(*request, inputs, report, wait);
        }
        const auto result = pg::abi::ResolveCollectionGroup(graph, plan, *request, report, wait);
        return result ? pg::Status{} : std::unexpected(result.error());
    };
    for (int kind = 0; kind < 3; ++kind)
    {
        PendingFile pending(output / "canonical.json");
        check(pending.IsMarked(), "establish native pending output");
        pg::abi::FailureReport noWait;
        check(Denied(operation(kind, pg::abi::CollectionOutputWait(), noWait)),
              "default reader remains fail closed without waiting");
        pg::abi::FailureReport report;
        for (std::size_t index = 0; index < pg::abi::FailureReport::kCapacity; ++index)
        {
            (void)report.Add(pg::abi::ToolError::kArguments, "retained_before_validation");
        }
        std::atomic<bool> entered{false};
        std::atomic<bool> finished{false};
        bool succeeded = false;
        std::jthread reader(
            [&]
            {
                entered = true;
                succeeded = operation(kind, pg::abi::CollectionOutputWait::Waiting(), report).has_value();
                finished = true;
            });
        const auto startDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!entered && std::chrono::steady_clock::now() < startDeadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        check(entered && !finished, "reader waits before accepting a pending output");
        pending.Release();
        reader.join();
        check(succeeded && report.Count == pg::abi::FailureReport::kCapacity && !report.Truncated,
              "reader revalidates after release without consuming caller report budget");

        const PendingFile permanent(output / "canonical.json");
        pg::abi::FailureReport boundedReport;
        const auto before = std::chrono::steady_clock::now();
        const auto status = operation(kind, {.Deadline = before + std::chrono::milliseconds(200)}, boundedReport);
        const auto elapsed = std::chrono::steady_clock::now() - before;
        check(permanent.IsMarked() && Denied(status) && boundedReport.Count == 1 &&
                  elapsed >= std::chrono::milliseconds(150) && elapsed < std::chrono::seconds(5),
              "shared absolute deadline bounds permanent denial and retains native error");
    }
    const PendingFile deniedInput(root / "database.json");
    pg::abi::FailureReport inputReport;
    const auto before = std::chrono::steady_clock::now();
    check(deniedInput.IsMarked() && Denied(operation(1, pg::abi::CollectionOutputWait::Waiting(), inputReport)) &&
              std::chrono::steady_clock::now() - before < std::chrono::seconds(5),
          "input access denial is not retried as a generated output");
    return failures == 0 ? 0 : 1;
}
