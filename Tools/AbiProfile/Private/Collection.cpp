#include "Collection.h"
#include "AndroidCollector.h"
#include "AppleCollector.h"
#include "CollectionRequest.h"
#include "CollectionState.h"
#include "CompileCommands.h"
#include "DependencyGraph.h"
#include "LinuxCollector.h"
#include "Result.h"
#include "ToolReport.h"
#include "WindowsCollector.h"

#include <expected>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace pg::abi
{
Result<CompileObservation> InspectCompileInputs(const CollectionRequest& request, std::span<const CompileUnit> units,
                                                FailureReport& report)
{
    if (request.HostPlatform == Platform::kWindows && request.TargetPlatform == Platform::kWindows)
    {
        return InspectWindowsCompileInputs(request, units, report);
    }
    if (request.HostPlatform == Platform::kLinux && request.TargetPlatform == Platform::kLinux)
    {
        return InspectLinuxCompileInputs(request, units, report);
    }
    if (request.HostPlatform == Platform::kMacos &&
        (request.TargetPlatform == Platform::kMacos || request.TargetPlatform == Platform::kIos))
    {
        return InspectAppleCompileInputs(request, units, report);
    }
    if (request.HostPlatform == Platform::kWindows && request.TargetPlatform == Platform::kAndroid)
    {
        return InspectAndroidCompileInputs(request, units, report);
    }
    // No synthetic evidence is accepted while a backend is unavailable. No synthetic evidence is accepted
    // through the production CLI while a platform backend is unavailable.
    return std::unexpected(report.Add(ToolError::kInvalidRequest, "platform_observation_unsupported"));
}
Result<CollectedProfile> CollectPlatformProfile(const CollectionRequest& request, std::span<const CompileUnit> units,
                                                const CompileObservation& observation, FailureReport& report)
{
    if (request.HostPlatform == Platform::kWindows && request.TargetPlatform == Platform::kWindows)
    {
        return CollectWindowsPlatformProfile(request, units, observation, report);
    }
    if (request.HostPlatform == Platform::kLinux && request.TargetPlatform == Platform::kLinux)
    {
        return CollectLinuxPlatformProfile(request, units, observation, report);
    }
    if (request.HostPlatform == Platform::kMacos &&
        (request.TargetPlatform == Platform::kMacos || request.TargetPlatform == Platform::kIos))
    {
        return CollectApplePlatformProfile(request, units, observation, report);
    }
    if (request.HostPlatform == Platform::kWindows && request.TargetPlatform == Platform::kAndroid)
    {
        return CollectAndroidPlatformProfile(request, units, observation, report);
    }
    return std::unexpected(report.Add(ToolError::kInvalidRequest, "platform_collection_unsupported"));
}
Status Collect(const CollectionRequest& request, FailureReport& report)
{
    // A failed collection must revoke the old generation before any probe.
    if (const auto invalidated = InvalidateCollection(request, report); !invalidated)
    {
        return invalidated;
    }
    const auto graph = ReadDependencyGraph(request, report);
    if (!graph)
    {
        return std::unexpected(graph.error());
    }
    const auto plan = PlanDependencies(*graph, request.TargetName, report);
    if (!plan)
    {
        return std::unexpected(plan.error());
    }
    const auto requests = ResolveCollectionGroup(*graph, *plan, request, report);
    if (!requests)
    {
        return std::unexpected(requests.error());
    }
    auto transaction = BeginCollectionTransaction(*requests, report);
    if (!transaction)
    {
        return std::unexpected(transaction.error());
    }
    std::vector<CollectedProfile> collected;
    std::vector<InputSnapshot> snapshots;
    std::map<std::string, nlohmann::json> profiles;
    for (const auto& member : *requests)
    {
        const auto units = LoadCompileUnits(member, report);
        if (!units)
        {
            return std::unexpected(units.error());
        }
        const auto observation = InspectCompileInputs(member, *units, report);
        if (!observation)
        {
            return std::unexpected(observation.error());
        }
        const auto snapshot = SnapshotInputs(member, *units, *observation, report);
        if (!snapshot)
        {
            return std::unexpected(snapshot.error());
        }
        auto profile = CollectPlatformProfile(member, *units, *observation, report);
        if (!profile)
        {
            return std::unexpected(profile.error());
        }
        profiles.emplace(member.TargetName, profile->Profile);
        snapshots.push_back(*snapshot);
        collected.push_back(std::move(*profile));
    }
    // External dependencies still need their own provenance-checked completion;
    // peers are checked together by the group publisher, never mutually Guarded.
    if (const auto compatible = VerifyCollectionDependencies(*graph, *plan, request, std::move(profiles), report);
        !compatible)
    {
        return compatible;
    }
    return (*transaction)->Publish(collected, snapshots);
}

int RunCompilerInvocation(const CollectionRequest& request, std::span<const std::string> arguments,
                          FailureReport& report)
{
#ifdef _WIN32
    constexpr auto kHost = Platform::kWindows;
#elifdef __APPLE__
    constexpr auto kHost = Platform::kMacos;
#else
    constexpr auto kHost = Platform::kLinux;
#endif
    if (arguments.empty() || request.HostPlatform != kHost)
    {
        (void)report.Add(ToolError::kArguments, "compile_arguments_or_host");
        return 2;
    }
    std::error_code error;
    const auto directory = std::filesystem::current_path(error);
    if (error)
    {
        (void)report.Add(ToolError::kResolvePath, "compile_directory", {}, error);
        return 2;
    }
    const auto unit = MatchCompilerInvocation(request, arguments, directory, report);
    if (!unit)
    {
        return 2;
    }
    if (const auto invalidated = InvalidateCollection(request, report); !invalidated)
    {
        return 3;
    }
    const auto units = LoadCompileUnits(request, report);
    if (!units)
    {
        return 2;
    }
    const auto observation = InspectCompileInputs(request, *units, report);
    return observation ? RunObservedCompilerInvocation(request, *unit, *observation, report) : 3;
}
} // namespace pg::abi
