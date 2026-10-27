#include "../../BuildTools/AbiProfile/Private/Process.h"
#include "../../BuildTools/AbiProfile/Private/ToolReport.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <system_error>
#include <thread>

namespace
{
std::string ReadLine(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    std::string line;
    std::getline(stream, line);
    return line;
}
} // namespace

int main(int argc, char* const* argv)
{
    if (argc != 6)
    {
        return 2;
    }
    const auto args = std::span(argv, static_cast<std::size_t>(argc));
    const auto cmake = std::filesystem::absolute(args.subspan(1).front());
    const auto source = std::filesystem::absolute(args.subspan(2).front());
    const auto smoke = std::filesystem::absolute(args.subspan(3).front());
    const auto testRoot = std::filesystem::absolute(args.subspan(4).front()) / "BuildToolsBootstrap" /
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code error;
    std::filesystem::create_directories(testRoot, error);
    if (error)
    {
        std::cerr << "Unable to create build tool test root: " << error.message() << '\n';
        return 1;
    }

    const auto buildToolsRoot = testRoot / "SharedBuildTools";
    const auto poison = testRoot / "TargetToolchain.cmake";
    {
        std::ofstream toolchain(poison);
        toolchain << "message(FATAL_ERROR \"Target toolchain leaked into native build tool bootstrap\")\n";
    }
    const auto systemPoison = testRoot / "CachedTargetSystem.cmake";
    {
        std::ofstream toolchain(systemPoison);
        toolchain << "set(CMAKE_SYSTEM_NAME \"${CMAKE_HOST_SYSTEM_NAME}\")\n";
    }
    {
        // Real system detection persists CMAKE_CROSSCOMPILING and the old
        // toolchain include in CMakeSystem.cmake, beyond the cache key itself.
        pg::abi::ProcessRequest process;
        process.Program = cmake;
        process.Directory = source;
        process.Timeout = std::chrono::minutes(2);
        process.Arguments = {
            "-S",
            (source / "BuildTools/AbiProfile/Bootstrap").generic_string(),
            "-B",
            (buildToolsRoot / "build").generic_string(),
            "-G",
            "Ninja",
            "-DPANGU_BUILD_TOOLS_SOURCE_ROOT=" + source.generic_string(),
            "-DCMAKE_CXX_COMPILER=" + std::filesystem::absolute(args.subspan(5).front()).generic_string(),
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_TOOLCHAIN_FILE=" + systemPoison.generic_string(),
        };
        pg::abi::FailureReport report;
        const auto result = pg::abi::RunProcess(process, report);
        if (!result || result->ExitCode == 0 || !result->Stderr.contains("ABI build tool must never be cross-compiled"))
        {
            std::cerr << "Could not establish the real failed cross-target build tool configuration\n";
            if (result)
            {
                std::cerr << result->Stdout << result->Stderr;
            }
            return 1;
        }
    }
    std::array<std::string, 2> diagnostics;
    std::array<int, 2> exitCodes{-1, -1};
    std::array<std::jthread, 2> workers;
    for (std::size_t i = 0; i < workers.size(); ++i)
    {
        workers.at(i) = std::jthread(
            [&, i]
            {
                const auto resultFile = testRoot / ("result-" + std::to_string(i) + ".txt");
                pg::abi::ProcessRequest process;
                process.Program = cmake;
                process.Directory = source;
                process.Timeout = std::chrono::minutes(9);
                process.Environment["CMAKE_TOOLCHAIN_FILE"] = poison.generic_string();
                process.Arguments = {
                    "-DPANGU_BUILD_TOOLS_SOURCE_ROOT=" + source.generic_string(),
                    "-DPANGU_BUILD_TOOLS_TEST_ROOT=" + buildToolsRoot.generic_string(),
                    "-DPANGU_BUILD_TOOLS_RESULT_FILE=" + resultFile.generic_string(),
                    "-DCMAKE_BUILD_TYPE=" + std::string(i == 0 ? "Debug" : "Release"),
                    "-DCMAKE_MSVC_RUNTIME_LIBRARY=" + std::string(i == 0 ? "MultiThreaded" : "MultiThreadedDLL"),
                    "-DANDROID_ABI=" + std::string(i == 0 ? "arm64-v8a" : ""),
                    "-DCMAKE_OSX_SYSROOT=" + std::string(i == 0 ? "iphoneos" : ""),
                    "-P",
                    smoke.generic_string(),
                };
                pg::abi::FailureReport report;
                const auto result = pg::abi::RunProcess(process, report);
                if (!result)
                {
                    diagnostics.at(i) = "Failed to execute CMake bootstrap";
                    return;
                }
                exitCodes.at(i) = result->ExitCode;
                diagnostics.at(i) = result->Stdout + result->Stderr;
            });
    }
    for (auto& worker : workers)
    {
        worker.join();
    }
    for (std::size_t i = 0; i < workers.size(); ++i)
    {
        if (exitCodes.at(i) != 0)
        {
            std::cerr << "Parallel build tool bootstrap " << i << " failed: " << diagnostics.at(i) << '\n';
            return 1;
        }
    }
    const auto first = ReadLine(testRoot / "result-0.txt");
    const auto second = ReadLine(testRoot / "result-1.txt");
    if (first.empty() || first != second || !std::filesystem::is_regular_file(first))
    {
        std::cerr << "Concurrent build tool bootstrap did not publish the same immutable tool\n";
        return 1;
    }
    pg::abi::ProcessRequest publishedProcess;
    publishedProcess.Program = first;
    publishedProcess.Directory = source;
    publishedProcess.Arguments = {"--help"};
    pg::abi::FailureReport publishedReport;
    const auto publishedResult = pg::abi::RunProcess(publishedProcess, publishedReport);
    if (!publishedResult || publishedResult->ExitCode != 2 ||
        !publishedResult->Stderr.contains("Usage: PanguAbiProfileTool"))
    {
        std::cerr << "Published Build tool cannot execute as a native program\n";
        return 1;
    }
    const auto published = std::filesystem::path(first).parent_path();
    if (!std::filesystem::is_regular_file(published / "ready.sha256"))
    {
        std::cerr << "Build tool publication lacks a ready digest\n";
        return 1;
    }
    const auto cache = buildToolsRoot / "build/CMakeCache.txt";
    std::ifstream stream(cache);
    const auto contents = std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    if (!contents.contains("CMAKE_BUILD_TYPE:STRING=Release") || contents.contains("CMAKE_CROSSCOMPILING:") ||
        contents.contains("ANDROID_ABI:") || contents.contains("CMAKE_OSX_SYSROOT:STRING=iphoneos") ||
        contents.contains(poison.generic_string()))
    {
        std::cerr << "Build tool bootstrap inherited a Target configuration or non-Release build\n";
        return 1;
    }
    std::cout << "Parallel native build tool bootstrap published once to " << first << '\n';
    return 0;
}
