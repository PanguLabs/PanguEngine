#pragma once

#include "CollectionRequest.h"
#include "ToolReport.h"

#include <Result.h>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace pg::abi
{
enum class DependencyKind : std::uint8_t
{
    kModule,
    kStatic,
    kInterface,
    kImported,
    kShared,
    kC,
    kObject,
};

struct DependencyEdge
{
    std::string Target;
    std::string Scope;
};

struct DependencyTarget
{
    std::string Name;
    DependencyKind Kind{};
    std::string Language;
    std::filesystem::path RequestFile;
    bool Managed{};
    bool SourceVisible{};
    std::vector<DependencyEdge> Links;
};

struct DependencyGraph
{
    std::map<std::string, DependencyTarget> Targets;
    std::map<std::string, std::string> Aliases;
};

struct DependencyPlan
{
    std::vector<std::string> Targets;
    std::vector<DependencyEdge> OrderedLinks;
    // Only actual STATIC members of cyclic components; INTERFACE propagation
    // stays in Targets/OrderedLinks and never requires a collection request.
    std::vector<std::vector<std::string>> TechnicalCycles;
    // Dependency-first compilation/attestation batches. Technical STATIC SCC
    // members appear together; INTERFACE nodes are ordering-only (no Profile).
    std::vector<std::vector<std::string>> CollectionGroups;
};

[[nodiscard]] Result<DependencyGraph> ReadDependencyGraph(const CollectionRequest& request, FailureReport& report);
[[nodiscard]] Result<DependencyPlan> PlanDependencies(const DependencyGraph& graph, std::string_view rootTarget,
                                                      FailureReport& report);
// The root's SCC only, deterministically ordered for lock acquisition. Acyclic
// roots return a singleton. All SCC members must name the same exported graph.
[[nodiscard]] Result<std::vector<CollectionRequest>> ResolveCollectionGroup(const DependencyGraph& graph,
                                                                            const DependencyPlan& plan,
                                                                            const CollectionRequest& root,
                                                                            FailureReport& report);
// Inputs must already have been verified against the frozen v1 schema and
// their individual build-completion records; comparison does not attest provenance.
[[nodiscard]] Status CompareDependencyProfiles(const DependencyGraph& graph, const DependencyPlan& plan,
                                               const std::map<std::string, nlohmann::json>& profiles,
                                               FailureReport& report);
// For every reachable compiled STATIC, validate an independently completed
// same-build request through GuardCollection before admitting its v1 Profile.
[[nodiscard]] Status VerifyDependencyProfiles(const DependencyGraph& graph, const DependencyPlan& plan,
                                              const CollectionRequest& rootRequest, const nlohmann::json& rootProfile,
                                              FailureReport& report);
// Staged profiles are borrowed from the current group transaction; their
// receipts and archive binding must pass PublishCollectionGroup before sealing.
[[nodiscard]] Status VerifyCollectionDependencies(const DependencyGraph& graph, const DependencyPlan& plan,
                                                  const CollectionRequest& rootRequest,
                                                  std::map<std::string, nlohmann::json> profiles,
                                                  FailureReport& report);
} // namespace pg::abi
