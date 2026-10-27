#include "../../BuildTools/AbiProfile/Private/Collection.h"
#include "../../BuildTools/AbiProfile/Private/CollectionRequest.h"
#include "../../BuildTools/AbiProfile/Private/CollectionState.h"
#include "../../BuildTools/AbiProfile/Private/CompileCommands.h"
#include "../../BuildTools/AbiProfile/Private/ProbeProcess.h"
#include "../../BuildTools/AbiProfile/Private/Process.h"
#include "../../BuildTools/AbiProfile/Private/ToolReport.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h> // IWYU pragma: keep - SDK prerequisites for isolated downstream fixtures.
#include <handleapi.h>
#include <jobapi2.h>
#include <minwinbase.h>
#include <minwindef.h>
#include <namedpipeapi.h>
#include <processenv.h>
#include <processthreadsapi.h>
#include <synchapi.h>
#include <winbase.h>
#include <winnt.h>
#else
#include <array>
#include <signal.h> // NOLINT(modernize-deprecated-headers) - POSIX kill is a native API.
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/signal.h>
#endif
#endif

namespace
{
// A test-only watchdog turns a blocked-forwarding regression into a bounded failure.
bool CheckForwarding(const std::filesystem::path& fixture, const std::filesystem::path& directory,
                     const std::string& mode)
{
    const auto marker = directory / ("forward-" + mode);
    const auto resultPath = std::filesystem::path(marker.string() + ".result");
    std::filesystem::remove(marker);
    std::filesystem::remove(resultPath);
    std::filesystem::remove(marker.string() + ".started");
    const auto started = std::chrono::steady_clock::now();
    int exitCode = -1;
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = nullptr,
        .bInheritHandle = TRUE,
    };
    HANDLE read = nullptr;
    HANDLE write = nullptr;
    if (::CreatePipe(&read, &write, &security, 4096) == 0)
    {
        return false;
    }
    (void)::SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    const std::wstring wideMode(mode.begin(), mode.end());
    auto command =
        L"\"" + fixture.native() + L"\" forward \"" + fixture.native() + L"\" \"" + marker.native() + L"\" " + wideMode;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write;
    startup.hStdError = write;
    startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION info{};
    auto* const job = ::CreateJobObjectW(nullptr, nullptr);
    if (::CreateProcessW(fixture.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr,
                         &startup, &info) == 0)
    {
        (void)::CloseHandle(read);
        (void)::CloseHandle(write);
        (void)::CloseHandle(job);
        return false;
    }
    (void)::AssignProcessToJobObject(job, info.hProcess);
    (void)::ResumeThread(info.hThread);
    (void)::CloseHandle(write);
    if (mode == "closed")
    {
        (void)::CloseHandle(read);
        read = nullptr;
    }
    if (::WaitForSingleObject(info.hProcess, 5000) == WAIT_OBJECT_0)
    {
        DWORD value = 0;
        (void)::GetExitCodeProcess(info.hProcess, &value);
        exitCode = static_cast<int>(value);
    }
    else
    {
        (void)::TerminateJobObject(job, 1);
    }
    (void)::WaitForSingleObject(info.hProcess, INFINITE);
    if (read != nullptr)
    {
        (void)::CloseHandle(read);
    }
    (void)::CloseHandle(info.hThread);
    (void)::CloseHandle(info.hProcess);
#else
    std::array<int, 2> pipe{};
    if (::pipe(pipe.data()) != 0)
    {
        return false;
    }
    const auto& program = fixture.native();
    const auto& markerText = marker.native();
    const auto pid = ::fork();
    if (pid < 0)
    {
        (void)::close(pipe.front());
        (void)::close(pipe.back());
        return false;
    }
    if (pid == 0)
    {
        (void)::close(pipe.front());
        (void)::dup2(pipe.back(), STDOUT_FILENO);
        (void)::dup2(pipe.back(), STDERR_FILENO);
        (void)::close(pipe.back());
        // Fixed native exec argument list, terminated explicitly by a null pointer.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        ::execl(program.c_str(), program.c_str(), "forward", program.c_str(), markerText.c_str(), mode.c_str(),
                nullptr);
        ::_exit(99);
    }
    (void)::close(pipe.back());
    if (mode == "closed")
    {
        (void)::close(pipe.front());
    }
    int status = 0;
    bool reaped = false;
    while (std::chrono::steady_clock::now() - started < std::chrono::seconds(5))
    {
        // Public sys/wait.h reexports platform wait macros.
        // NOLINTNEXTLINE(misc-include-cleaner)
        if (::waitpid(pid, &status, WNOHANG) == pid)
        {
            reaped = true;
            // NOLINTNEXTLINE(misc-include-cleaner)
            exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!reaped)
    {
        (void)::kill(pid, SIGKILL);
        (void)::waitpid(pid, &status, 0);
    }
    if (mode != "closed")
    {
        (void)::close(pipe.front());
    }
#endif
    const auto elapsed = std::chrono::steady_clock::now() - started;
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    std::ifstream file(resultPath);
    std::string stage;
    std::chrono::milliseconds::rep operationMilliseconds = -1;
    file >> stage >> operationMilliseconds;
    std::string_view wanted = "process_output";
    if (mode.starts_with("cancel"))
    {
        wanted = "process_cancelled";
    }
    if (mode == "timeout")
    {
        wanted = "process_timeout";
    }
    // The POSIX-only disposition check below updates this shared test result.
    // NOLINTNEXTLINE(misc-const-correctness)
    bool passed = exitCode == 23 && stage == wanted && operationMilliseconds >= 0 && operationMilliseconds < 1000 &&
                  std::filesystem::exists(marker.string() + ".started") && !std::filesystem::exists(marker);
#ifndef _WIN32
    {
        std::ifstream startFile(marker.string() + ".started");
        int producer = 0;
        std::string disposition;
        startFile >> producer >> disposition;
        passed &= producer > 0 && disposition == "default";
    }
    // On RED a dead wrapper can orphan its independently grouped producer.
    if (!passed)
    {
        std::ifstream pidFile(marker.string() + ".started");
        int producer = 0;
        pidFile >> producer;
        if (producer > 0)
        {
            (void)::kill(-producer, SIGKILL);
        }
    }
#endif
    // Cleanup follows the assertions, so the test cannot hide a leaked producer.
#ifdef _WIN32
    (void)::TerminateJobObject(job, 1);
    (void)::CloseHandle(job);
#endif
    std::cout << "forward " << mode << ": exit=" << exitCode << " stage=" << stage
              << " elapsed_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
              << " operation_ms=" << operationMilliseconds << " marker=" << std::filesystem::exists(marker) << '\n';
    return passed;
}
} // namespace

int main(int argc, char* const* argv)
{
    if (argc != 4)
    {
        return 2;
    }
    const auto args = std::span(argv, static_cast<std::size_t>(argc));
    const auto directory = std::filesystem::absolute(args.back()) / "Process work space";
    std::filesystem::create_directories(directory);
    pg::abi::ProcessRequest request;
    request.Program = std::filesystem::absolute(args.subspan(1).front());
    request.Directory = directory;
    int failures = 0;
    const auto check = [&failures](bool passed, const char* name)
    {
        if (!passed)
        {
            std::cerr << "FAIL: " << name << '\n';
            ++failures;
        }
    };
    check(CheckForwarding(request.Program, directory, "cancel"),
          "unread downstream cancellation is bounded and cleans child");
    check(CheckForwarding(request.Program, directory, "cancel-delayed"),
          "cancellation latency excludes unrelated executable startup delay");
    check(CheckForwarding(request.Program, directory, "timeout"),
          "unread downstream timeout is bounded and cleans child");
    check(CheckForwarding(request.Program, directory, "closed"), "closed downstream fails normally and cleans child");
    pg::abi::FailureReport report;
    request.Arguments = {"exit"};
    const auto exited = pg::abi::RunProcess(request, report);
    check(exited && exited->ExitCode == 17 && exited->Stderr == "compiler diagnostic\n", "exit 17 and diagnostic");
    {
        pg::abi::FailureReport probeReport;
        // The stream buffer is mutated indirectly by std::cerr.
        // NOLINTNEXTLINE(misc-const-correctness)
        std::ostringstream discardedLog;
        auto* originalStream = std::cerr.rdbuf(discardedLog.rdbuf());
        const auto failedProbe = pg::abi::RunProbe(request, "probe_report_first", probeReport);
        (void)probeReport.Add(pg::abi::ToolError::kInvalidProfile, "probe_report_first");
        const auto laterProbe = pg::abi::RunProbe(request, "probe_report_later", probeReport);
        std::cerr.rdbuf(originalStream);
        std::ostringstream delivered;
        probeReport.WriteJson(delivered);
        const auto document = nlohmann::json::parse(delivered.str(), nullptr, false);
        check(failedProbe && failedProbe->ExitCode == 17 && laterProbe && laterProbe->ExitCode == 17 &&
                  document.contains("probe") && document.at("probe").at("exit_code") == 17 &&
                  document.at("probe").at("stage") == "probe_report_first" &&
                  document.at("probe").at("stderr") == "compiler diagnostic\n",
              "failure report owns the first probe evidence without stderr logs");
    }
    {
        pg::abi::FailureReport boundedReport;
        request.Arguments = {"failure-flood", std::string(1024, 'a')};
        // The stream buffer is mutated indirectly by std::cerr.
        // NOLINTNEXTLINE(misc-const-correctness)
        std::ostringstream discardedLog;
        auto* originalStream = std::cerr.rdbuf(discardedLog.rdbuf());
        const auto failedProbe = pg::abi::RunProbe(request, "bounded_probe_report", boundedReport);
        std::cerr.rdbuf(originalStream);
        (void)boundedReport.Add(pg::abi::ToolError::kInvalidProfile, "bounded_probe_report");
        std::ostringstream delivered;
        boundedReport.WriteJson(delivered);
        const auto document = nlohmann::json::parse(delivered.str(), nullptr, false);
        check(failedProbe && failedProbe->ExitCode == 17 && document.contains("probe") &&
                  document.at("probe").at("stdout").get_ref<const std::string&>().size() == 8192 &&
                  document.at("probe").at("stderr").get_ref<const std::string&>().size() == 32768 &&
                  document.at("probe").at("arguments").at(1).get_ref<const std::string&>().size() == 256 &&
                  document.at("probe").at("truncated") == true && document.at("truncated") == true,
              "probe evidence owns bounded argv and output with explicit truncation");
    }

    const std::vector<std::string> values{
        "",
        "a b",
        "quote\"inside",
        "back\\slash\\",
        "ends with space ",
        "\\\"",
        "中文-é-🌍",
        "$(echo BAD); & | > < `x`",
        "\n\t",
    };
    request.Arguments = {"echo"};
    request.Arguments.insert(request.Arguments.end(), values.begin(), values.end());
    std::string expected;
    for (const auto& value : values)
    {
        expected += std::to_string(value.size()) + ':' + value + '\n';
    }
    const auto echoed = pg::abi::RunProcess(request, report);
    check(echoed && echoed->ExitCode == 0 && echoed->Stdout == expected,
          "literal argv including Unicode and shell text");

    request.Arguments = {"flood"};
    const auto flooded = pg::abi::RunProcess(request, report);
    check(flooded && flooded->Stdout == std::string(std::size_t{1024} * 1024, 'x') &&
              flooded->Stderr == flooded->Stdout,
          "concurrent stdout stderr 1 MiB each");
    request.CaptureLimitBytes = 1024;
    pg::abi::FailureReport limitReport;
    check(!pg::abi::RunProcess(request, limitReport) && limitReport.Count > 0, "capture limit fails");
    request.CaptureLimitBytes = std::size_t{16} * 1024 * 1024;

    request.Arguments = {"context"};
    request.Environment = {{"PANGU_PROCESS_TEST", "literal $(x); 中文"}};
    const auto context = pg::abi::RunProcess(request, report);
    check(context && context->Stdout == directory.generic_string() + "\nliteral $(x); 中文",
          "cwd and environment override");

    const auto timeoutMarker = directory / "timeout-marker";
    const auto cancelMarker = directory / "cancel-marker";
    std::filesystem::remove(timeoutMarker);
    std::filesystem::remove(cancelMarker);
    request.Arguments = {"sleep", timeoutMarker.generic_string()};
    request.Timeout = std::chrono::milliseconds(40);
    pg::abi::FailureReport timeoutReport;
    check(!pg::abi::RunProcess(request, timeoutReport) && timeoutReport.Count > 0, "timeout fails");

    request.Timeout = std::chrono::seconds(10);
    request.Arguments.back() = cancelMarker.generic_string();
    std::stop_source cancellation;
    request.Cancellation = cancellation.get_token();
    std::jthread const canceller(
        [&cancellation]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            (void)cancellation.request_stop();
        });
    pg::abi::FailureReport cancelReport;
    check(!pg::abi::RunProcess(request, cancelReport) && cancelReport.Count > 0, "cancellation fails");
    request.Cancellation = {};
    std::this_thread::sleep_for(std::chrono::milliseconds(750));
    check(!std::filesystem::exists(timeoutMarker) && !std::filesystem::exists(cancelMarker), "children terminated");
    request.Program = directory / "no-such-program";
    pg::abi::FailureReport startReport;
    check(!pg::abi::RunProcess(request, startReport) && startReport.Count > 0, "launch failure");

    pg::abi::CollectionRequest collection;
    collection.Compiler = std::filesystem::absolute(args.subspan(1).front());
    collection.PlatformContext = nlohmann::json::object();
#ifdef _WIN32
    collection.HostPlatform = pg::abi::Platform::kWindows;
#elifdef __APPLE__
    collection.HostPlatform = pg::abi::Platform::kMacos;
#else
    collection.HostPlatform = pg::abi::Platform::kLinux;
#endif
    const auto compilerText = collection.Compiler.generic_u8string();
    const std::string compiler(compilerText.begin(), compilerText.end());
    const auto build = directory / "build";
    std::filesystem::create_directories(build);
    collection.SourceRoot = directory;
    collection.TargetName = "Fixture";
    collection.Configuration = "Debug";
    collection.BuildRoot = build;
    collection.OutputDirectory = build / "Abi/Debug/Fixture";
    collection.SignatureHeader = collection.OutputDirectory / "inputs.h";
    collection.Library = build / "fixture.a";
    collection.LinkGraph = build / "links.json";
    collection.SchemaFile = directory / "schema.json";
    {
        std::ofstream file(collection.LinkGraph);
        file << "{}";
    }
    {
        std::ofstream file(collection.SchemaFile);
        file << "{}";
    }
    collection.Database = build / "compile_commands.json";
    collection.ObjectManifest = build / "objects.txt";
    const auto compilerArguments = [&](std::vector<std::string> values, const std::filesystem::path& cwd)
    {
        {
            std::ofstream file(cwd / "fixture.cpp");
            file << "// internal process receipt fixture\n";
        }
        values.insert(values.end(), {"-c", "fixture.cpp", "-o", "fixture.o"});
        const nlohmann::json database = nlohmann::json::array({
            {
                {"directory", cwd.generic_string()},
                {"file", "fixture.cpp"},
                {"output", (cwd / "fixture.o").generic_string()},
                {"arguments", values},
            },
        });
        {
            std::ofstream file(collection.Database);
            file << database.dump();
        }
        {
            std::ofstream file(collection.ObjectManifest);
            file << (cwd / "fixture.o").generic_string() << '\n';
        }
        return values;
    };
    const auto failedArguments = compilerArguments({compiler, "exit"}, build);
    pg::abi::FailureReport compilerReport;
    const auto failedUnit = pg::abi::MatchCompilerInvocation(collection, failedArguments, build, compilerReport);
    const pg::abi::CompileObservation processObservation;
    check(failedUnit &&
              pg::abi::RunObservedCompilerInvocation(collection, *failedUnit, processObservation, compilerReport) == 17,
          "observed compiler failure preserves exact exit code");
    const auto succeededArguments =
        compilerArguments({compiler, "echo", "actual argument"}, std::filesystem::current_path());
    pg::abi::FailureReport unavailableReport;
    check(pg::abi::RunCompilerInvocation(collection, succeededArguments, unavailableReport) != 0 &&
              unavailableReport.Count > 0,
          "success cannot publish an unverified receipt");
    pg::abi::FailureReport mismatchReport;
    check(pg::abi::RunCompilerInvocation(collection, std::vector<std::string>{"ccache", compiler, "exit"},
                                         mismatchReport) != 0 &&
              mismatchReport.Count > 0,
          "compiler launcher is rejected");

    const auto utf8 = [](const std::filesystem::path& path)
    {
        const auto text = path.generic_u8string();
        return std::string(text.begin(), text.end());
    };
    const auto output = build / "Abi/Debug/Fixture";
    const std::u8string requestName = u8"compile 请求.json";
    const auto requestFile = directory / requestName;
#ifdef _WIN32
    const auto* host = "windows";
    const auto* style = "windows";
#elifdef __APPLE__
    const auto* host = "macos";
    const auto* style = "posix";
#else
    const auto* host = "linux";
    const auto* style = "posix";
#endif
    const nlohmann::json cliRequest{
        {"schema", 2},
        {"source_root", utf8(directory)},
        {"build_root", utf8(build)},
        {"target_name", "Fixture"},
        {"configuration", "Debug"},
        {"host_platform", host},
        {"target_platform", host},
        {"command_line_style", style},
        {"compiler", compiler},
        {"database", utf8(build / "compile_commands.json")},
        {"object_manifest", utf8(build / "objects.txt")},
        {"library", utf8(build / "fixture.a")},
        {"output_directory", utf8(output)},
        {"schema_file", utf8(directory / "schema.json")},
        {"signature_header", utf8(output / "signature.h")},
        {"link_graph", utf8(build / "links.json")},
        {"platform_context", nlohmann::json::object()},
        {"environment_baseline", nlohmann::json::object()},
    };
    {
        std::ofstream file(requestFile);
        file << cliRequest.dump();
    }
    pg::abi::ProcessRequest cli;
    cli.Program = std::filesystem::absolute(args.subspan(2).front());
    cli.Directory = directory;
    const auto cliFailedArgs = compilerArguments({compiler, "exit"}, directory);
    cli.Arguments = {"compile", "--request", utf8(requestFile), "--"};
    cli.Arguments.insert(cli.Arguments.end(), cliFailedArgs.begin(), cliFailedArgs.end());
    pg::abi::FailureReport cliReport;
    const auto cliFailure = pg::abi::RunProcess(cli, cliReport);
    std::string_view expectedStage = "platform_observation_unsupported";
    switch (collection.HostPlatform)
    {
    case pg::abi::Platform::kWindows:
        expectedStage = "windows_llvm_companions";
        break;
    case pg::abi::Platform::kLinux:
        expectedStage = "linux_runtime_sources";
        break;
    case pg::abi::Platform::kMacos:
        // The fixture is not an installed Clang toolchain: configurable
        // prefixes are accepted only with their verified libc++ resources.
        expectedStage = "apple_unverified_libcxx";
        break;
    default:
        break;
    }
    if (cliFailure && (cliFailure->ExitCode != 3 || !cliFailure->Stdout.contains(expectedStage)))
    {
        std::cerr << "compile fixture diagnostic: exit=" << cliFailure->ExitCode << " " << cliFailure->Stdout << '\n';
    }
    check(cliFailure && cliFailure->ExitCode == 3 && cliFailure->Stdout.contains(expectedStage) &&
              !cliFailure->Stdout.contains("compiler diagnostic"),
          "compile CLI Unicode request rejects untrusted launcher before execution");
    const auto cliSucceededArgs = compilerArguments({compiler, "echo", "literal 中文 $(x); \\"}, directory);
    cli.Arguments = {"compile", "--request", utf8(requestFile), "--"};
    cli.Arguments.insert(cli.Arguments.end(), cliSucceededArgs.begin(), cliSucceededArgs.end());
    const auto cliSuccess = pg::abi::RunProcess(cli, cliReport);
    const bool refusedBeforeExecution = cliSuccess && cliSuccess->ExitCode == 3 &&
                                        cliSuccess->Stdout.contains(expectedStage) &&
                                        !cliSuccess->Stdout.contains("literal");
    if (cliSuccess && !refusedBeforeExecution)
    {
        std::cerr << "compile fixture diagnostic: exit=" << cliSuccess->ExitCode << " " << cliSuccess->Stdout << '\n';
    }
    check(refusedBeforeExecution && !std::filesystem::exists(output / "complete.json"),
          "compile CLI cannot bypass real compiler or source provenance checks");
    return failures == 0 ? 0 : 1;
}
