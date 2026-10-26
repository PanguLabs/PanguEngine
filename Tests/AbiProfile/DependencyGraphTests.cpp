#include "../../Tools/AbiProfile/Private/CollectionRequest.h"
#include "../../Tools/AbiProfile/Private/DependencyGraph.h"
#include "../../Tools/AbiProfile/Private/ToolIo.h"
#include "../../Tools/AbiProfile/Private/ToolReport.h"

#include <algorithm>
#include <cstddef>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

int main(int argc, char* const argv[])
{
    if (argc != 3)
    {
        return 2;
    }
    const auto args = std::span(argv, static_cast<std::size_t>(argc));
    const auto directory = std::filesystem::absolute(args.subspan<1>().front()) / "DependencyGraphCases";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        return 1;
    }
    // Preserve exact declared order, link repetitions, private links and aliases.
    const nlohmann::json good = {
        {"schema", 1},
        {"format", "cmake-actual-v1"},
        {"configuration", "Release"},
        {"aliases", nlohmann::json::array({{{"name", "AliasA"}, {"target", "TechA"}}})},
        {
            "targets",
            nlohmann::json::array({
                {
                    {"name", "Root"},
                    {"kind", "module"},
                    {"language", "cxx"},
                    {"managed", true},
                    {"source_visible", true},
                    {
                        "links",
                        nlohmann::json::array({
                            {{"target", "AliasA"}, {"scope", "private"}},
                            {{"target", "Options"}, {"scope", "interface"}},
                            {{"target", "AliasA"}, {"scope", "private"}},
                        }),
                    },
                },
                {
                    {"name", "TechA"},
                    {"kind", "static"},
                    {"language", "cxx"},
                    {"managed", false},
                    {"source_visible", true},
                    {"links", nlohmann::json::array({{{"target", "TechB"}, {"scope", "private"}}})},
                },
                {
                    {"name", "TechB"},
                    {"kind", "static"},
                    {"language", "cxx"},
                    {"managed", false},
                    {"source_visible", true},
                    {"links", nlohmann::json::array({{{"target", "TechA"}, {"scope", "private"}}})},
                },
                {
                    {"name", "Options"},
                    {"kind", "interface"},
                    {"language", "header"},
                    {"managed", false},
                    {"source_visible", true},
                    {"links", nlohmann::json::array({{{"target", "TechB"}, {"scope", "interface"}}})},
                },
            }),
        },
    };
    pg::abi::CollectionRequest request;
    request.LinkGraph = directory / "links.json";
    request.Configuration = "Release";
    pg::abi::FailureReport report;
    if (!pg::abi::WriteFile(request.LinkGraph, good.dump(), report))
    {
        return 1;
    }
    const auto graph = pg::abi::ReadDependencyGraph(request, report);
    if (!graph)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    const auto plan = pg::abi::PlanDependencies(*graph, "Root", report);
    if (!plan || plan->TechnicalCycles.size() != 1 || plan->TechnicalCycles.front().size() != 2 ||
        plan->OrderedLinks.size() != 6 || plan->OrderedLinks.front().Target != "TechA" ||
        plan->OrderedLinks.at(2).Target != "TechA")
    {
        report.WriteJson(std::cerr);
        std::cerr << "Declared order/alias/SCC/PRIVATE dependencies were not preserved\n";
        return 1;
    }
    // SCC collection is a single batch: each reachable compiled dependency
    // must be in exactly one group, scheduled before its consuming Module.
    // INTERFACE targets influence order but never invent a compiled Profile.
    if (plan->CollectionGroups.size() != 2)
    {
        std::cerr << "Technical SCC did not collapse into dependency-first collection groups\n";
        return 1;
    }
    const auto& technical = plan->CollectionGroups.front();
    const auto& consuming = plan->CollectionGroups.back();
    if (technical.size() != 2 || consuming.size() != 1 || consuming.front() != "Root" ||
        std::ranges::find(technical, "TechA") == technical.end() ||
        std::ranges::find(technical, "TechB") == technical.end())
    {
        std::cerr << "SCC collection groups split archives or lost dependency ordering\n";
        return 1;
    }
    auto acyclic = good;
    acyclic.at("targets").at(2).at("links") = nlohmann::json::array();
    if (!pg::abi::WriteFile(request.LinkGraph, acyclic.dump(), report))
    {
        return 1;
    }
    const auto acyclicGraph = pg::abi::ReadDependencyGraph(request, report);
    if (!acyclicGraph)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    const auto acyclicPlan = pg::abi::PlanDependencies(*acyclicGraph, "Root", report);
    if (!acyclicPlan || !acyclicPlan->TechnicalCycles.empty() || acyclicPlan->CollectionGroups.size() != 3 ||
        acyclicPlan->CollectionGroups.at(0) != std::vector<std::string>{"TechB"} ||
        acyclicPlan->CollectionGroups.at(1) != std::vector<std::string>{"TechA"} ||
        acyclicPlan->CollectionGroups.at(2) != std::vector<std::string>{"Root"})
    {
        report.WriteJson(std::cerr);
        std::cerr << "Acyclic STATIC dependency scheduling is not dependency-first\n";
        return 1;
    }
    auto imported = good;
    imported.at("targets").push_back({
        {"name", "External"},
        {"kind", "imported"},
        {"language", "cxx"},
        {"managed", false},
        {"source_visible", false},
        {"links", nlohmann::json::array()},
    });
    imported.at("targets").at(0).at("links").push_back({{"target", "External"}, {"scope", "private"}});
    if (!pg::abi::WriteFile(request.LinkGraph, imported.dump(), report))
    {
        return 1;
    }
    const auto importedGraph = pg::abi::ReadDependencyGraph(request, report);
    pg::abi::FailureReport expected;
    if (!importedGraph || pg::abi::PlanDependencies(*importedGraph, "Root", expected))
    {
        std::cerr << "Precompiled imported C++ library was accepted\n";
        return 1;
    }
    auto cyclicModules = good;
    cyclicModules.at("targets").at(1).at("kind") = "module";
    cyclicModules.at("targets").at(1).at("managed") = true;
    if (!pg::abi::WriteFile(request.LinkGraph, cyclicModules.dump(), report))
    {
        return 1;
    }
    const auto moduleGraph = pg::abi::ReadDependencyGraph(request, report);
    if (!moduleGraph || pg::abi::PlanDependencies(*moduleGraph, "Root", expected))
    {
        std::cerr << "A managed Module cycle was silently treated as technical SCC\n";
        return 1;
    }
    const auto rejectsGraph = [&](const nlohmann::json& changed, std::string_view label)
    {
        pg::abi::FailureReport invalid;
        if (!pg::abi::WriteFile(request.LinkGraph, changed.dump(), invalid))
        {
            invalid.WriteJson(std::cerr);
            return false;
        }
        const auto parsed = pg::abi::ReadDependencyGraph(request, invalid);
        if (parsed && pg::abi::PlanDependencies(*parsed, "Root", invalid))
        {
            std::cerr << "Dependency graph accepted unsupported boundary: " << label << '\n';
            return false;
        }
        return true;
    };
    auto plainC = good;
    plainC.at("targets").at(2).at("kind") = "c";
    plainC.at("targets").at(2).at("language") = "c";
    auto shared = good;
    shared.at("targets").at(2).at("kind") = "shared";
    auto unknownLink = good;
    unknownLink.at("targets").at(0).at("links").at(0).at("target") = "NonexistentExternalTarget";
    auto unresolved = good;
    unresolved.at("targets").at(0).at("links").at(0).at("target") = "$<LINK_ONLY:TechA>";
    auto interfaceCycle = good;
    interfaceCycle.at("targets").at(3).at("links").push_back({{"target", "Root"}, {"scope", "interface"}});
    auto wrongConfig = good;
    wrongConfig.at("configuration") = "Debug";
    if (!rejectsGraph(plainC, "plain C library") || !rejectsGraph(shared, "shared library") ||
        !rejectsGraph(unknownLink, "unknown target") || !rejectsGraph(unresolved, "raw LINK_ONLY expression") ||
        !rejectsGraph(interfaceCycle, "cycle containing a managed Module") ||
        !rejectsGraph(wrongConfig, "wrong configuration"))
    {
        return 1;
    }
    // The graph alone is not an ABI attestation. Even after per-Target
    // provenance is established by the caller, every compiled dependency's
    // validated v1 Profile must agree field by field (not just its digest).
    const auto vectorsText = pg::abi::ReadFile(args[2], "read_v1_vectors", report);
    if (!vectorsText)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    const auto vectors = nlohmann::json::parse(*vectorsText, nullptr, false);
    if (vectors.is_discarded() || !vectors.contains("baseline"))
    {
        std::cerr << "Missing frozen v1 baseline fixture\n";
        return 1;
    }
    const auto baseline = vectors.at("baseline").at("profile");
    // INTERFACE nodes propagate dependencies inside an archive SCC, but are
    // never independently compiled or assigned a synthetic Profile.
    auto interfaceBridge = good;
    interfaceBridge.at("targets").at(1).at("links") =
        nlohmann::json::array({{{"target", "Options"}, {"scope", "private"}}});
    interfaceBridge.at("targets").at(3).at("links").push_back({{"target", "Leaf"}, {"scope", "interface"}});
    interfaceBridge.at("targets").push_back({
        {"name", "Leaf"},
        {"kind", "static"},
        {"language", "cxx"},
        {"managed", false},
        {"source_visible", true},
        {"links", nlohmann::json::array()},
    });
    if (!pg::abi::WriteFile(request.LinkGraph, interfaceBridge.dump(), report))
    {
        return 1;
    }
    const auto bridgeGraph = pg::abi::ReadDependencyGraph(request, report);
    if (!bridgeGraph)
    {
        report.WriteJson(std::cerr);
        return 1;
    }
    const auto bridgePlan = pg::abi::PlanDependencies(*bridgeGraph, "Root", report);
    if (!bridgePlan || bridgePlan->Targets != std::vector<std::string>{"Root", "TechA", "Options", "TechB", "Leaf"} ||
        bridgePlan->OrderedLinks.size() != 7 || bridgePlan->TechnicalCycles.size() != 1 ||
        bridgePlan->TechnicalCycles.front().size() != 2 ||
        bridgePlan->CollectionGroups != std::vector<std::vector<std::string>>{{"Leaf"}, {"TechA", "TechB"}, {"Root"}})
    {
        report.WriteJson(std::cerr);
        std::cerr << "INTERFACE bridge split the technical SCC or scheduled its external dependency too late\n";
        return 1;
    }
    const std::map<std::string, nlohmann::json> bridgeProfiles{
        {"Root", baseline},
        {"TechA", baseline},
        {"TechB", baseline},
        {"Leaf", baseline},
    };
    if (!pg::abi::CompareDependencyProfiles(*bridgeGraph, *bridgePlan, bridgeProfiles, report))
    {
        report.WriteJson(std::cerr);
        std::cerr << "INTERFACE bridge required a fake header Profile\n";
        return 1;
    }
    auto managedBridge = interfaceBridge;
    managedBridge.at("targets").at(2).at("kind") = "module";
    managedBridge.at("targets").at(2).at("managed") = true;
    if (!rejectsGraph(managedBridge, "managed Module cycle through INTERFACE"))
    {
        return 1;
    }
    auto interfaceOnly = interfaceBridge;
    interfaceOnly.at("targets").at(0).at("links") =
        nlohmann::json::array({{{"target", "Options"}, {"scope", "private"}}});
    interfaceOnly.at("targets").at(3).at("links") = nlohmann::json::array(
        {{{"target", "Facade"}, {"scope", "interface"}}, {{"target", "Leaf"}, {"scope", "interface"}}});
    interfaceOnly.at("targets").push_back({
        {"name", "Facade"},
        {"kind", "interface"},
        {"language", "header"},
        {"managed", false},
        {"source_visible", true},
        {"links", nlohmann::json::array({{{"target", "Options"}, {"scope", "interface"}}})},
    });
    if (!pg::abi::WriteFile(request.LinkGraph, interfaceOnly.dump(), report))
    {
        return 1;
    }
    const auto interfaceGraph = pg::abi::ReadDependencyGraph(request, report);
    if (!interfaceGraph)
    {
        return 1;
    }
    const auto interfacePlan = pg::abi::PlanDependencies(*interfaceGraph, "Root", report);
    if (!interfacePlan || !interfacePlan->TechnicalCycles.empty() || interfacePlan->OrderedLinks.size() != 4 ||
        interfacePlan->CollectionGroups != std::vector<std::vector<std::string>>{{"Leaf"}, {"Root"}} ||
        !pg::abi::CompareDependencyProfiles(*interfaceGraph, *interfacePlan, {{"Root", baseline}, {"Leaf", baseline}},
                                            report))
    {
        report.WriteJson(std::cerr);
        std::cerr << "Pure INTERFACE cycle invented a compiled group or lost a propagated dependency\n";
        return 1;
    }
    std::map<std::string, nlohmann::json> verifiedProfiles{
        {"Root", baseline},
        {"TechA", baseline},
        {"TechB", baseline},
    };
    pg::abi::FailureReport compareReport;
    if (!pg::abi::CompareDependencyProfiles(*graph, *plan, verifiedProfiles, compareReport))
    {
        compareReport.WriteJson(std::cerr);
        return 1;
    }
    verifiedProfiles.at("TechA").at("stdlib").at("iterator_debug_level") = "2";
    pg::abi::FailureReport mismatchReport;
    if (pg::abi::CompareDependencyProfiles(*graph, *plan, verifiedProfiles, mismatchReport) ||
        mismatchReport.Count != 1 ||
        mismatchReport.Entries.front().JsonPointer.View() != "/stdlib/iterator_debug_level")
    {
        std::cerr << "Cross-target ABI difference was not pinpointed\n";
        return 1;
    }
    verifiedProfiles.at("TechA") = baseline;
    verifiedProfiles.erase("TechB");
    pg::abi::FailureReport missingReport;
    if (pg::abi::CompareDependencyProfiles(*graph, *plan, verifiedProfiles, missingReport) ||
        missingReport.Count != 1 || missingReport.Entries.front().Path.View() != "TechB")
    {
        std::cerr << "Missing technical STATIC Profile was not rejected\n";
        return 1;
    }
    return 0;
}
