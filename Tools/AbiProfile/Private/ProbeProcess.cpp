#include "ProbeProcess.h"
#include "Process.h"
#include "ToolReport.h"

#include <Result.h>

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace pg::abi
{
namespace
{
std::string PathText(const std::filesystem::path& path)
{
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}
} // namespace

Result<ProcessResult> RunProbe(const ProcessRequest& request, std::string_view stage, FailureReport& report)
{
    auto result = RunProcess(request, report);
    if (result && result->ExitCode != 0)
    {
        auto& evidence = *report.Probe;
        // Preserve the first failed probe; cleanup or fallback cannot replace it.
        if (!evidence.Present)
        {
            evidence.Present = true;
            evidence.ExitCode = result->ExitCode;
            evidence.Truncated |= evidence.Stage.Assign(stage);
            evidence.Truncated |= evidence.Program.Assign(PathText(request.Program));
            evidence.Truncated |= evidence.Directory.Assign(PathText(request.Directory));
            for (const auto& argument : request.Arguments)
            {
                if (evidence.ArgumentCount == ProbeFailure::kArgumentCapacity)
                {
                    evidence.Truncated = true;
                    break;
                }
                evidence.Truncated |= evidence.Arguments.at(evidence.ArgumentCount++).Assign(argument);
            }
            evidence.Truncated |= evidence.Stdout.Assign(result->Stdout);
            evidence.Truncated |= evidence.Stderr.Assign(result->Stderr);
            report.Truncated |= evidence.Truncated;
        }
        auto diagnostic = evidence.Json();
        diagnostic.emplace("operation", "abi-probe");
        std::cerr << diagnostic.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
    }
    return result;
}
} // namespace pg::abi
