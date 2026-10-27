#include "TechnicalClosure.h"

#include "Collection.h"
#include "CollectionRequest.h"
#include "CollectionState.h"
#include "DependencyGraph.h"
#include "ToolReport.h"

#include <Result.h>

#include <expected>

namespace pg::abi
{
namespace
{
[[nodiscard]] Status ProcessClosure(const CollectionRequest& root, bool publish, FailureReport& report)
{
    const auto graph = ReadDependencyGraph(root, report);
    if (!graph)
    {
        return std::unexpected(graph.error());
    }
    const auto plan = PlanDependencies(*graph, root.TargetName, report);
    if (!plan)
    {
        return std::unexpected(plan.error());
    }
    for (const auto& group : plan->CollectionGroups)
    {
        if (group.empty())
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "technical_group_empty"));
        }
        const auto& first = graph->Targets.at(group.front());
        if (first.Kind != DependencyKind::kStatic)
        {
            continue;
        }
        // PlanDependencies owns grouping and SCC ordering. Never attempt to
        // order individual members of the same component in CMake.
        for (const auto& name : group)
        {
            if (graph->Targets.at(name).Kind != DependencyKind::kStatic)
            {
                return std::unexpected(report.Add(ToolError::kInvalidProfile, "technical_group_mixed", name));
            }
        }
        if (first.RequestFile.empty())
        {
            return std::unexpected(
                report.Add(ToolError::kInvalidRequest, "technical_group_request_missing", first.Name));
        }
        const auto request = ReadCollectionRequest(first.RequestFile, report);
        if (!request)
        {
            return std::unexpected(request.error());
        }
        if (request->TargetName != first.Name || request->BuildRoot != root.BuildRoot ||
            request->SourceRoot != root.SourceRoot || request->Configuration != root.Configuration ||
            request->HostPlatform != root.HostPlatform || request->TargetPlatform != root.TargetPlatform)
        {
            return std::unexpected(report.Add(ToolError::kInvalidRequest, "technical_group_identity", first.Name));
        }
        const auto state = GuardCollection(*request, report);
        if (!state)
        {
            return std::unexpected(state.error());
        }
        if (publish && *state != GuardDisposition::kCurrent)
        {
            if (const auto collected = Collect(*request, report); !collected)
            {
                return std::unexpected(collected.error());
            }
        }
    }
    return {};
}
} // namespace

Status GuardTechnicalClosure(const CollectionRequest& root, FailureReport& report)
{
    return ProcessClosure(root, false, report);
}

Status CollectTechnicalClosure(const CollectionRequest& root, FailureReport& report)
{
    return ProcessClosure(root, true, report);
}
} // namespace pg::abi
