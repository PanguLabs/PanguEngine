#include "../../BuildTools/AbiProfile/Private/CollectionRequest.h"
#include "../../BuildTools/AbiProfile/Private/DependencyGraph.h"
#include "../../BuildTools/AbiProfile/Private/ToolReport.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

int main(int argc, char* const argv[])
{
    if (argc != 4)
    {
        return 2;
    }
    const auto args = std::span(argv, static_cast<std::size_t>(argc));
    pg::abi::CollectionRequest request;
    request.LinkGraph = std::filesystem::path(args.subspan<1>().front());
    request.Configuration = args.subspan<2>().front();
    const std::string_view kind = args.subspan<3>().front();
    pg::abi::FailureReport report;
    const auto graph = pg::abi::ReadDependencyGraph(request, report);
    if (kind == "invalid")
    {
        if (graph)
        {
            std::cerr << "C++ accepted transitive source compilation without TU provenance\n";
            return 1;
        }
        return 0;
    }
    const bool cycleFixture = kind == "bridge" || kind == "managed-bridge" || kind == "interface-only";
    if (!graph || graph->Targets.size() != (cycleFixture ? 6U : 5U) || graph->Aliases.at("AliasA") != "TechA")
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    const auto expectedRequest = request.LinkGraph.parent_path() / "TechA-request.json";
    if (graph->Targets.at("TechA").RequestFile != expectedRequest || !graph->Targets.at("TechB").RequestFile.empty())
    {
        std::cerr << "CMake did not export the per-STATIC collection request path\n";
        return 1;
    }
    const auto& root = graph->Targets.at("Root");
    if (cycleFixture)
    {
        const auto plan = pg::abi::PlanDependencies(*graph, "Root", report);
        if (kind == "managed-bridge")
        {
            if (plan || report.Count == 0 ||
                report.Entries.front().Stage.View() != "dependency_module_or_interface_cycle")
            {
                report.WriteJson(std::cerr);
                std::cerr << "Managed Module cycle through INTERFACE was accepted\n";
                return 1;
            }
            return 0;
        }
        const auto expectedGroups = kind == "bridge"
                                        ? std::vector<std::vector<std::string>>{{"Leaf"}, {"TechA", "TechB"}, {"Root"}}
                                        : std::vector<std::vector<std::string>>{{"TechA", "TechB"}, {"Root"}};
        if (!plan || plan->CollectionGroups != expectedGroups || plan->TechnicalCycles.size() != 1 ||
            plan->TechnicalCycles.front().size() != 2 ||
            std::ranges::find(plan->TechnicalCycles.front(), "TechA") == plan->TechnicalCycles.front().end() ||
            std::ranges::find(plan->TechnicalCycles.front(), "TechB") == plan->TechnicalCycles.front().end())
        {
            report.WriteJson(std::cerr);
            std::cerr << "CMake INTERFACE cycle lost archive grouping or external dependency order\n";
            return 1;
        }
        return 0;
    }
    if (root.Links.size() != (request.Configuration == "Release" && kind != "imported" ? 4U : 5U) ||
        root.Links.front().Target != "AliasA" || root.Links.front().Scope != "direct" ||
        root.Links.at(1).Target != "Options" || root.Links.at(2).Target != "TechA" ||
        root.Links.at(3).Target != "TechA" || (kind == "imported" && root.Links.at(4).Target != "Prebuilt") ||
        graph->Targets.at("Options").Links.size() != (request.Configuration == "Release" ? 1U : 0U))
    {
        std::cerr << "CMake evaluated links, config branch, duplicate or alias mismatch\n";
        return 1;
    }
    const auto plan = pg::abi::PlanDependencies(*graph, "Root", report);
    if (kind == "imported")
    {
        if (plan)
        {
            std::cerr << "Prebuilt C++ dependency bypassed the graph verifier\n";
            return 1;
        }
        return 0;
    }
    if (!plan || plan->TechnicalCycles.size() != 1 || plan->TechnicalCycles.front().size() != 2 ||
        plan->OrderedLinks.size() != 7 || plan->CollectionGroups.size() != 2 ||
        plan->CollectionGroups.front().size() != 2 || plan->CollectionGroups.back().size() != 1 ||
        plan->CollectionGroups.back().front() != "Root")
    {
        report.WriteJson(std::cerr);
        std::cerr << "CMake target graph failed to preserve transitive technical SCC closure\n";
        return 1;
    }
    return 0;
}
