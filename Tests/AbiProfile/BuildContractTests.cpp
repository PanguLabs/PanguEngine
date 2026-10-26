#include "../../Tools/AbiProfile/Private/CollectionRequest.h"
#include "../../Tools/AbiProfile/Private/CollectionState.h"
#include "../../Tools/AbiProfile/Private/Process.h"
#include "../../Tools/AbiProfile/Private/ToolReport.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
using pg::abi::ProcessRequest;
using pg::abi::ProcessResult;

std::optional<ProcessResult> Invoke(const std::filesystem::path& cmake, const std::filesystem::path& cwd,
                                    std::vector<std::string> arguments,
                                    std::map<std::string, std::string> environment = {})
{
    ProcessRequest request;
    request.Program = cmake;
    request.Directory = cwd;
    request.Arguments = std::move(arguments);
    request.Environment = std::move(environment);
    request.Timeout = std::chrono::minutes(9);
    pg::abi::FailureReport report;
    auto result = pg::abi::RunProcess(request, report);
    if (!result)
    {
        report.WriteJson(std::cerr);
        return std::nullopt;
    }
    if (result->ExitCode != 0)
    {
        std::cerr << result->Stdout << '\n' << result->Stderr << '\n';
    }
    return std::move(*result);
}

std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
} // namespace

int main(int argc, char* const* argv)
{
    if (argc != 5 && argc != 6)
    {
        return 2;
    }
    const auto args = std::span(argv, static_cast<std::size_t>(argc));
    const auto mode = argc == 6 ? std::string_view(args.back()) : std::string_view("direct");
    if (mode != "direct" && mode != "transitive" && mode != "conditional-debug" && mode != "environment" &&
        mode != "diagnostic" && mode != "interface-cycle" && mode != "completion-recovery")
    {
        return 2;
    }
    const bool transitive = mode == "transitive" || mode == "conditional-debug";
    const bool technicalSelected = mode != "conditional-debug";
    const auto* configuration = technicalSelected ? "Release" : "Debug";
    const auto cmake = std::filesystem::absolute(args.subspan(1).front());
    const auto compiler = std::filesystem::absolute(args.subspan(2).front());
    const auto source = std::filesystem::absolute(args.subspan(3).front());
    const auto work = std::filesystem::absolute(args.subspan(4).front()) / "BuildContract" /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code error;
    std::filesystem::create_directories(work, error);
    if (error)
    {
        std::cerr << "Unable to prepare private test root: " << error.message() << '\n';
        return 1;
    }
    const auto fixture = source / "Tests/AbiProfile/Fixtures/BuildContract";
    std::vector<std::string> configure = {
        "-S",
        fixture.generic_string(),
        "-B",
        work.generic_string(),
        "-G",
        "Ninja",
        "-DCMAKE_CXX_COMPILER=" + compiler.generic_string(),
        "-DCMAKE_BUILD_TYPE=" + std::string(configuration),
        "-DPANGU_ABI_SOURCE_ROOT=" + source.generic_string(),
    };
    if (mode == "interface-cycle")
    {
        configure.emplace_back("-DPANGU_ABI_INTERFACE_CYCLE=ON");
    }
    if (transitive)
    {
        configure.emplace_back("-DPANGU_ABI_TRANSITIVE_TECH=ON");
    }
    if (!technicalSelected)
    {
        configure.emplace_back("-DPANGU_ABI_CONDITIONAL_TECH=ON");
    }
#ifdef __APPLE__
    configure.push_back("-DCMAKE_TOOLCHAIN_FILE=" + (source / "cmake/Platform/macOS/Toolchain.cmake").generic_string());
#elifndef _WIN32
    configure.push_back("-DCMAKE_TOOLCHAIN_FILE=" + (source / "cmake/Platform/Linux/Toolchain.cmake").generic_string());
#endif
    std::map<std::string, std::string> baselineEnvironment;
    if (mode == "environment")
    {
        baselineEnvironment["LIBPATH"] = std::string(R"(C:\PanguReview\quoted "path";$<CONFIG>)") + "\tline\nnext";
    }
    const auto configured = Invoke(cmake, work, std::move(configure), baselineEnvironment);
    if (!configured || configured->ExitCode != 0)
    {
        return 1;
    }
    const std::vector<std::string> build = {
        "--build", work.generic_string(), "--target", "FixtureModule", "--parallel", "4",
    };
    if (mode == "diagnostic")
    {
        {
            std::ofstream output(work / "Base.cpp", std::ios::trunc);
            output << "#error PANGU_COMPILER_DIAGNOSTIC_REGRESSION\n";
        }
        const auto failed = Invoke(cmake, work, build);
        if (!failed || failed->ExitCode == 0 ||
            !(failed->Stdout + failed->Stderr).contains("PANGU_COMPILER_DIAGNOSTIC_REGRESSION") ||
            !failed->Stdout.contains(R"("operation":"abi-profile")"))
        {
            std::cerr << "Compiler preflight lost the original diagnostic or structured failure report\n";
            return 1;
        }
        return 0;
    }
    const auto first = Invoke(cmake, work, build, baselineEnvironment);
    const auto abi = work / "Abi" / configuration;
    const auto complete = abi / "FixtureModule/complete.json";
    const auto baseComplete = abi / "FixtureBase/complete.json";
    const auto techAComplete = abi / "TechA/complete.json";
    const auto techBComplete = abi / "TechB/complete.json";
    const auto groupSeal = abi / "TechA/group-complete.json";
    const bool technicalValid =
        technicalSelected
            ? std::filesystem::is_regular_file(techAComplete) && std::filesystem::is_regular_file(techBComplete) &&
                  std::filesystem::is_regular_file(groupSeal)
            : !std::filesystem::exists(techAComplete) && !std::filesystem::exists(techBComplete) &&
                  !std::filesystem::exists(work / "TechA.lib") && !std::filesystem::exists(work / "libTechA.a");
    if (!first || first->ExitCode != 0 || !std::filesystem::is_regular_file(baseComplete) ||
        !std::filesystem::is_regular_file(complete) || !technicalValid)
    {
        std::cerr << "Module closure did not match the selected technical STATIC SCC\n";
        return 1;
    }
    if (mode == "completion-recovery")
    {
        pg::abi::FailureReport report;
        const auto request = pg::abi::ReadCollectionRequest(abi / "FixtureBase/request.json", report);
        if (!request || !pg::abi::InvalidateCollection(*request, report))
        {
            report.WriteJson(std::cerr);
            return 1;
        }
        const auto restored = Invoke(cmake, work, build);
        if (!restored || restored->ExitCode != 0 || !std::filesystem::is_regular_file(baseComplete) ||
            !std::filesystem::is_regular_file(complete) || !restored->Stdout.contains("recollected") ||
            restored->Stdout.contains("Building CXX object"))
        {
            std::cerr << "Trusted unchanged artifacts did not recover their invalidated completion\n";
            return 1;
        }
        return 0;
    }
    if (mode == "environment")
    {
        const auto* variable = "CPATH";
        auto environment = baselineEnvironment;
        environment[variable] = (work / "changed-includes").string();
        const auto changedEnvironment = Invoke(cmake, work, build, std::move(environment));
        if (!changedEnvironment || changedEnvironment->ExitCode == 0 || std::filesystem::exists(baseComplete) ||
            !changedEnvironment->Stdout.contains("environment_requires_reconfigure"))
        {
            std::cerr << "Production Module Guard accepted changed compiler environment\n";
            return 1;
        }
        return 0;
    }
    const auto before = Read(complete);
    const auto beforeTechSeal = technicalSelected ? Read(groupSeal) : std::string{};
    const auto baseSource = work / "Base.cpp";
    auto contents = Read(baseSource);
    const auto changed = contents.find("+ 35");
    if (changed == std::string::npos)
    {
        std::cerr << "Private dependency fixture source has changed unexpectedly\n";
        return 1;
    }
    contents.replace(changed, 4, "+ 36");
    {
        std::ofstream output(baseSource, std::ios::binary | std::ios::trunc);
        output << contents;
        if (!output)
        {
            std::cerr << "Unable to edit private dependency build fixture\n";
            return 1;
        }
    }
    const auto second = Invoke(cmake, work, build);
    if (!second || second->ExitCode != 0 || !std::filesystem::is_regular_file(complete) || before == Read(complete) ||
        (technicalSelected && beforeTechSeal != Read(groupSeal)) || !second->Stdout.contains("recollected") ||
        second->Stdout.contains("Building CXX object CMakeFiles/FixtureModule.dir/"))
    {
        std::cerr << "Updated Base did not recollect unchanged Module without recompilation\n";
        if (second)
        {
            std::cerr << second->Stdout << '\n';
        }
        return 1;
    }
    const auto third = Invoke(cmake, work, build);
    if (!third || third->ExitCode != 0 || !third->Stdout.contains("current") ||
        third->Stdout.contains("Building CXX object"))
    {
        std::cerr << "Unchanged ABI Module build failed to reuse its completed generation\n";
        return 1;
    }
    std::cout << "Native ABI contract: managed Modules and technical STATIC SCC, dependency-only recollection, cache "
                 "passed\n";
    return 0;
}
