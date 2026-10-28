#include "DependencyGraph.h"

#include "CollectionRequest.h"
#include "CollectionState.h"
#include "JsonSchema.h"
#include "Result.h"
#include "ToolIo.h"
#include "ToolReport.h"

#include <array>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pg::abi
{
namespace
{
using Json = nlohmann::json;

[[nodiscard]] Result<DependencyKind> ParseKind(std::string_view text, FailureReport& report)
{
    if (text == "module")
    {
        return DependencyKind::kModule;
    }
    if (text == "static")
    {
        return DependencyKind::kStatic;
    }
    if (text == "interface")
    {
        return DependencyKind::kInterface;
    }
    if (text == "imported")
    {
        return DependencyKind::kImported;
    }
    if (text == "shared")
    {
        return DependencyKind::kShared;
    }
    if (text == "c")
    {
        return DependencyKind::kC;
    }
    if (text == "object")
    {
        return DependencyKind::kObject;
    }
    return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_unknown_kind", text));
}

[[nodiscard]] bool StringProperty(const Json& object, std::string_view key)
{
    const auto found = object.find(std::string(key));
    return found != object.end() && found->is_string() && !found->get<std::string>().empty();
}

[[nodiscard]] bool KeySet(const Json& object, std::span<const std::string_view> names)
{
    if (!object.is_object() || object.size() != names.size())
    {
        return false;
    }
    return std::ranges::all_of(names, [&](std::string_view key) { return object.contains(std::string(key)); });
}

[[nodiscard]] bool ScopeValid(std::string_view scope)
{
    return scope == "private" || scope == "public" || scope == "interface" || scope == "link-only" || scope == "direct";
}

[[nodiscard]] Result<std::string> Resolve(const DependencyGraph& graph, std::string_view input, FailureReport& report)
{
    std::string name(input);
    if (const auto alias = graph.Aliases.find(name); alias != graph.Aliases.end())
    {
        if (graph.Aliases.contains(alias->second) || !graph.Targets.contains(alias->second))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_invalid_alias", name));
        }
        return alias->second;
    }
    if (!graph.Targets.contains(name))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_unknown_target", name));
    }
    return name;
}
[[nodiscard]] bool HeaderSource(std::string_view source)
{
    return source.ends_with(".h") || source.ends_with(".hh") || source.ends_with(".hpp") || source.ends_with(".hxx") ||
           source.ends_with(".inc") || source.ends_with(".inl");
}

// Generated CMake metadata contains facts, not ABI policy decisions.
// CMake evaluates expressions; this parser classifies and validates their results.
[[nodiscard]] Result<DependencyTarget> ReadGeneratedTarget(const Json& row, FailureReport& report)
{
    constexpr std::array<std::string_view, 10> kFields{"name",
                                                       "cmake_type",
                                                       "imported",
                                                       "managed",
                                                       "links",
                                                       "sources_evaluated",
                                                       "interface_sources_evaluated",
                                                       "request_evaluated",
                                                       "links_evaluated",
                                                       "interface_evaluated"};
    if (!KeySet(row, kFields) || !StringProperty(row, "name") || !StringProperty(row, "cmake_type") ||
        !row.at("imported").is_boolean() || !row.at("managed").is_boolean() || !row.at("links").is_array() ||
        !row.at("links").empty() || !row.at("sources_evaluated").is_string() ||
        !row.at("interface_sources_evaluated").is_string() || !row.at("request_evaluated").is_string() ||
        !row.at("links_evaluated").is_string() || !row.at("interface_evaluated").is_string())
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_generated_target_shape"));
    }

    DependencyTarget target;
    target.Name = row.at("name").get<std::string>();
    target.Managed = row.at("managed").get<bool>();
    const auto requestPath = row.at("request_evaluated").get<std::string>();
    if (!requestPath.empty())
    {
        const auto candidate = std::filesystem::path(requestPath);
        if (!candidate.is_absolute() || requestPath.find_first_of("$<>\r\n\"") != std::string::npos)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_request_path", target.Name));
        }
        target.RequestFile = candidate;
    }
    const auto type = row.at("cmake_type").get<std::string>();
    if (row.at("imported").get<bool>())
    {
        target.Kind = DependencyKind::kImported;
    }
    else if (type == "STATIC_LIBRARY")
    {
        target.Kind = target.Managed ? DependencyKind::kModule : DependencyKind::kStatic;
    }
    else if (type == "INTERFACE_LIBRARY")
    {
        target.Kind = DependencyKind::kInterface;
    }
    else if (type == "SHARED_LIBRARY" || type == "MODULE_LIBRARY")
    {
        target.Kind = DependencyKind::kShared;
    }
    else if (type == "OBJECT_LIBRARY")
    {
        target.Kind = DependencyKind::kObject;
    }
    else
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_unknown_target_type", target.Name));
    }

    bool hasCxx = false;
    bool hasC = false;
    bool knownSources = true;
    const auto inspect = [&](std::string_view field, bool transitive) -> Status
    {
        const auto sources = row.at(std::string(field)).get<std::string>();
        if (sources.size() > std::size_t{1024} * 1024)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_source_list_size", target.Name));
        }
        for (std::size_t cursor = 0; cursor < sources.size();)
        {
            const auto end = sources.find(';', cursor);
            const auto source =
                std::string_view(sources).substr(cursor, (end == std::string::npos ? sources.size() : end) - cursor);
            if (!source.empty())
            {
                if (source.find_first_of("$<>\r\n\"") != std::string_view::npos)
                {
                    return std::unexpected(
                        report.Add(ToolError::kInvalidProfile, "dependency_unresolved_source", target.Name));
                }
                if (transitive)
                {
                    // INTERFACE_SOURCES are compiled by consumers, even on STATIC libraries.
                    // Until these extra TUs can be tracked, only headers are supported.
                    if (!HeaderSource(source))
                    {
                        return std::unexpected(report.Add(ToolError::kInvalidProfile,
                                                          "dependency_transitive_source_not_header_only", target.Name));
                    }
                }
                else if (source.ends_with(".cpp") || source.ends_with(".cc") || source.ends_with(".cxx"))
                {
                    hasCxx = true;
                }
                else if (source.ends_with(".c"))
                {
                    hasC = true;
                }
                else if (!HeaderSource(source))
                {
                    knownSources = false;
                }
            }
            if (end == std::string::npos)
            {
                break;
            }
            cursor = end + 1;
        }
        return {};
    };
    if (const auto result = inspect("sources_evaluated", false); !result)
    {
        return std::unexpected(result.error());
    }
    if (const auto result = inspect("interface_sources_evaluated", true); !result)
    {
        return std::unexpected(result.error());
    }

    if (target.Kind == DependencyKind::kInterface)
    {
        target.Language = "header";
        target.SourceVisible = !hasCxx && !hasC && knownSources;
    }
    else
    {
        target.Language = hasC && !hasCxx ? "c" : "cxx";
        target.SourceVisible = !row.at("imported").get<bool>() && hasCxx && !hasC && knownSources;
    }
    return target;
}
} // namespace

Result<DependencyGraph> ReadDependencyGraph(const CollectionRequest& request, FailureReport& report)
{
    const auto raw = ReadFile(request.LinkGraph, "dependency_graph_read", report);
    if (!raw)
    {
        return std::unexpected(raw.error());
    }
    const auto json = ParseUnique(*raw);
    constexpr std::array<std::string_view, 5> kTop{"schema", "format", "configuration", "targets", "aliases"};
    if (!json || !KeySet(*json, kTop) || !json->at("schema").is_number_integer() || json->at("schema") != 1 ||
        json->at("format") != "cmake-actual-v1" || json->at("configuration") != request.Configuration ||
        !json->at("targets").is_array() || !json->at("aliases").is_array() || json->at("targets").empty() ||
        json->at("targets").size() > 4096)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_graph_schema"));
    }
    DependencyGraph graph;
    constexpr std::array<std::string_view, 6> kTarget{"name", "kind", "language", "managed", "source_visible", "links"};
    constexpr std::array<std::string_view, 7> kTargetWithRequest{"name",           "kind",  "language",    "managed",
                                                                 "source_visible", "links", "request_file"};
    constexpr std::array<std::string_view, 8> kEvaluatedTarget{
        "name", "kind", "language", "managed", "source_visible", "links", "links_evaluated", "interface_evaluated"};
    constexpr std::array<std::string_view, 2> kLink{"target", "scope"};
    for (const auto& row : json->at("targets"))
    {
        const bool generated = row.is_object() && row.contains("cmake_type");
        const bool evaluated = row.is_object() && row.contains("links_evaluated");
        DependencyTarget target;
        if (generated)
        {
            auto parsed = ReadGeneratedTarget(row, report);
            if (!parsed)
            {
                return std::unexpected(parsed.error());
            }
            target = std::move(*parsed);
        }
        else
        {
            bool validFields = false;
            if (evaluated)
            {
                validFields = KeySet(row, kEvaluatedTarget);
            }
            else if (row.contains("request_file"))
            {
                validFields = KeySet(row, kTargetWithRequest);
            }
            else
            {
                validFields = KeySet(row, kTarget);
            }
            if (!validFields || !StringProperty(row, "name") || !StringProperty(row, "kind") ||
                !StringProperty(row, "language") || !row.at("managed").is_boolean() ||
                !row.at("source_visible").is_boolean() || !row.at("links").is_array())
            {
                return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_target_shape"));
            }
            target.Name = row.at("name").get<std::string>();
            const auto kind = ParseKind(row.at("kind").get<std::string>(), report);
            if (!kind)
            {
                return std::unexpected(kind.error());
            }
            target.Kind = *kind;
            target.Managed = row.at("managed").get<bool>();
            target.SourceVisible = row.at("source_visible").get<bool>();
            target.Language = row.at("language").get<std::string>();
            if (row.contains("request_file"))
            {
                if (!StringProperty(row, "request_file"))
                {
                    return std::unexpected(
                        report.Add(ToolError::kInvalidProfile, "dependency_request_shape", target.Name));
                }
                target.RequestFile = row.at("request_file").get<std::string>();
                const auto spelling = target.RequestFile.generic_string();
                if (!target.RequestFile.is_absolute() || spelling.find_first_of("$<>\r\n\"") != std::string::npos)
                {
                    return std::unexpected(
                        report.Add(ToolError::kInvalidProfile, "dependency_request_path", target.Name));
                }
            }
        }
        const auto& language = target.Language;
        if ((target.Kind == DependencyKind::kInterface && language != "header") ||
            ((target.Kind == DependencyKind::kModule || target.Kind == DependencyKind::kStatic) && language != "cxx") ||
            ((target.Kind == DependencyKind::kImported || target.Kind == DependencyKind::kShared ||
              target.Kind == DependencyKind::kObject) &&
             language != "cxx" && language != "c") ||
            (target.Kind == DependencyKind::kModule && !target.Managed) ||
            (target.Kind != DependencyKind::kModule && target.Managed) ||
            (target.Kind == DependencyKind::kC && language != "c"))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_target_language", target.Name));
        }
        // CMAKE target names may contain "::", but never raw generator expressions
        // when exported as a resolved, current-configuration target identity.
        if (target.Name.find_first_of("$<;\r\n") != std::string::npos || row.at("links").size() > 16384)
        {
            return std::unexpected(
                report.Add(ToolError::kInvalidProfile, "dependency_unresolved_expression", target.Name));
        }
        for (const auto& link : row.at("links"))
        {
            if (!KeySet(link, kLink) || !StringProperty(link, "target") || !StringProperty(link, "scope"))
            {
                return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_link_shape", target.Name));
            }
            DependencyEdge edge{
                .Target = link.at("target").get<std::string>(),
                .Scope = link.at("scope").get<std::string>(),
            };
            if (!ScopeValid(edge.Scope) || edge.Target.find_first_of("$<;\r\n") != std::string::npos)
            {
                return std::unexpected(
                    report.Add(ToolError::kInvalidProfile, "dependency_link_expression", edge.Target));
            }
            target.Links.emplace_back(std::move(edge));
        }
        if (evaluated)
        {
            // CMake has already evaluated generator expressions in this exact
            // target context. Empty elements from false config branches are
            // skipped, but repeated direct dependencies retain their order.
            if (!row.at("links").empty() || !row.at("links_evaluated").is_string() ||
                !row.at("interface_evaluated").is_string())
            {
                return std::unexpected(
                    report.Add(ToolError::kInvalidProfile, "dependency_evaluated_links_shape", target.Name));
            }
            std::set<std::string> directlyLinked;
            for (const auto& [field, scope] : {
                     std::pair<std::string_view, std::string_view>{"links_evaluated", "direct"},
                     {"interface_evaluated", "interface"},
                 })
            {
                const auto rawLinks = row.at(std::string(field)).get<std::string>();
                if (rawLinks.size() > std::size_t{1024} * 1024)
                {
                    return std::unexpected(
                        report.Add(ToolError::kInvalidProfile, "dependency_evaluated_links_size", target.Name));
                }
                for (std::size_t cursor = 0; cursor < rawLinks.size();)
                {
                    const auto end = rawLinks.find(';', cursor);
                    const auto token = rawLinks.substr(cursor, end == std::string::npos ? end : end - cursor);
                    if (!token.empty())
                    {
                        if (token.find_first_of("$<>\r\n\t\"\\") != std::string::npos || token.starts_with('-') ||
                            token.contains('/') || (token.contains(':') && !token.contains("::")))
                        {
                            return std::unexpected(
                                report.Add(ToolError::kInvalidProfile, "dependency_unresolved_evaluated_link", token));
                        }
                        if (scope == "direct")
                        {
                            directlyLinked.insert(token);
                        }
                        if (scope == "direct" || !directlyLinked.contains(token))
                        {
                            target.Links.emplace_back(DependencyEdge{
                                .Target = token,
                                .Scope = std::string(scope),
                            });
                        }
                    }
                    if (end == std::string::npos)
                    {
                        break;
                    }
                    cursor = end + 1;
                }
            }
        }
        if (!graph.Targets.emplace(target.Name, std::move(target)).second)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_duplicate_target"));
        }
    }

    constexpr std::array<std::string_view, 2> kAlias{"name", "target"};
    for (const auto& entry : json->at("aliases"))
    {
        if (!KeySet(entry, kAlias) || !StringProperty(entry, "name") || !StringProperty(entry, "target"))
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_alias_shape"));
        }
        const auto alias = entry.at("name").get<std::string>();
        const auto canonical = entry.at("target").get<std::string>();
        if (graph.Targets.contains(alias) || alias.find_first_of("$<;\r\n") != std::string::npos ||
            !graph.Targets.contains(canonical) || !graph.Aliases.emplace(alias, canonical).second)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_alias_conflict", alias));
        }
    }
    // A graph with dangling edges or unresolved names must fail before any
    // MODULE Profile is published. Do not treat a missing edge as system CRT.
    for (const auto& [name, target] : graph.Targets)
    {
        for (const auto& edge : target.Links)
        {
            if (!Resolve(graph, edge.Target, report))
            {
                return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_unresolved_link", name));
            }
        }
    }
    return graph;
}

Result<DependencyPlan> PlanDependencies(const DependencyGraph& graph, std::string_view rootTarget,
                                        FailureReport& report)
{
    const auto root = Resolve(graph, rootTarget, report);
    if (!root)
    {
        return std::unexpected(root.error());
    }
    const auto& rootNode = graph.Targets.at(*root);
    // A managed Module may consume the closure, while each technical STATIC
    // must also be independently collectable without becoming a Pangu Module.
    if ((rootNode.Kind != DependencyKind::kModule && rootNode.Kind != DependencyKind::kStatic) ||
        (rootNode.Kind == DependencyKind::kModule && !rootNode.Managed) ||
        (rootNode.Kind == DependencyKind::kStatic && rootNode.Managed))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_root_not_compiled", *root));
    }

    struct Frame
    {
        std::string Name;
        std::size_t Next{};
    };
    DependencyPlan plan;
    std::set<std::string> visited;
    std::vector<Frame> walk;
    const auto enter = [&](const std::string& name) -> Status
    {
        if (visited.contains(name))
        {
            return {};
        }
        const auto& target = graph.Targets.at(name);
        if ((target.Kind != DependencyKind::kModule && target.Kind != DependencyKind::kStatic &&
             target.Kind != DependencyKind::kInterface) ||
            !target.SourceVisible)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_unverified_cpp_boundary", name));
        }
        visited.insert(name);
        plan.Targets.push_back(name);
        walk.emplace_back(Frame{.Name = name});
        return {};
    };
    if (const auto ready = enter(*root); !ready)
    {
        return std::unexpected(ready.error());
    }
    while (!walk.empty())
    {
        auto& frame = walk.back();
        const auto& links = graph.Targets.at(frame.Name).Links;
        if (frame.Next >= links.size())
        {
            walk.pop_back();
            continue;
        }
        const auto& edge = links.at(frame.Next++);
        const auto resolved = Resolve(graph, edge.Target, report);
        if (!resolved)
        {
            return std::unexpected(resolved.error());
        }
        plan.OrderedLinks.emplace_back(DependencyEdge{.Target = *resolved, .Scope = edge.Scope});
        if (const auto ready = enter(*resolved); !ready)
        {
            return std::unexpected(ready.error());
        }
    }

    // Non-recursive Kosaraju: bounded explicitly by known graph nodes, not
    // untrusted recursion depth. The first DFS visits each link in CMake order.
    std::set<std::string> firstPass;
    std::vector<std::string> finishing;
    for (const auto& start : plan.Targets)
    {
        if (!firstPass.insert(start).second)
        {
            continue;
        }
        std::vector<Frame> pending{Frame{.Name = start}};
        while (!pending.empty())
        {
            auto& frame = pending.back();
            const auto& edges = graph.Targets.at(frame.Name).Links;
            if (frame.Next >= edges.size())
            {
                finishing.push_back(frame.Name);
                pending.pop_back();
                continue;
            }
            const auto next = Resolve(graph, edges.at(frame.Next++).Target, report);
            if (!next || !visited.contains(*next))
            {
                return std::unexpected(
                    report.Add(ToolError::kInvalidProfile, "dependency_scc_unresolved_edge", frame.Name));
            }
            if (firstPass.insert(*next).second)
            {
                pending.emplace_back(Frame{.Name = *next});
            }
        }
    }
    std::map<std::string, std::vector<std::string>> reverseEdges;
    for (const auto& name : plan.Targets)
    {
        reverseEdges.emplace(name, std::vector<std::string>{});
    }
    for (const auto& name : plan.Targets)
    {
        for (const auto& link : graph.Targets.at(name).Links)
        {
            const auto resolved = Resolve(graph, link.Target, report);
            if (!resolved || !reverseEdges.contains(*resolved))
            {
                return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_reverse_graph", name));
            }
            reverseEdges.at(*resolved).push_back(name);
        }
    }
    std::set<std::string> assigned;
    std::vector<std::vector<std::string>> components;
    for (const auto& item : std::views::reverse(finishing))
    {
        if (!assigned.insert(item).second)
        {
            continue;
        }
        std::vector<std::string> component;
        std::vector<std::string> pending{item};
        while (!pending.empty())
        {
            const auto name = std::move(pending.back());
            pending.pop_back();
            component.push_back(name);
            for (const auto& predecessor : reverseEdges.at(name))
            {
                if (assigned.insert(predecessor).second)
                {
                    pending.push_back(predecessor);
                }
            }
        }
        bool selfLink = false;
        if (component.size() == 1)
        {
            for (const auto& edge : graph.Targets.at(component.front()).Links)
            {
                const auto destination = Resolve(graph, edge.Target, report);
                if (!destination)
                {
                    return std::unexpected(destination.error());
                }
                selfLink |= *destination == component.front();
            }
        }
        if (component.size() > 1 || selfLink)
        {
            std::vector<std::string> archives;
            for (const auto& name : component)
            {
                const auto kind = graph.Targets.at(name).Kind;
                if (kind == DependencyKind::kModule)
                {
                    return std::unexpected(
                        report.Add(ToolError::kInvalidProfile, "dependency_module_or_interface_cycle", name));
                }
                if (kind == DependencyKind::kStatic)
                {
                    archives.push_back(name);
                }
            }
            // INTERFACE nodes carry edges through the SCC, but have neither
            // archives nor collection requests. A pure propagation cycle is
            // valid and schedules only its reachable compiled dependencies.
            if (!archives.empty())
            {
                plan.TechnicalCycles.emplace_back(std::move(archives));
            }
        }
        components.emplace_back(std::move(component));
    }

    // Reverse the SCC topological order: technical dependency archives must
    // be collected before the Module consuming them, including INTERFACE
    // transitives. INTERFACE targets do not contribute an independent Profile.
    std::map<std::string, std::size_t> discoveryOrder;
    for (std::size_t i = 0; i < plan.Targets.size(); ++i)
    {
        discoveryOrder.emplace(plan.Targets.at(i), i);
    }
    for (const auto& component : std::views::reverse(components))
    {
        std::vector<std::string> compiled;
        for (const auto& name : component)
        {
            if (graph.Targets.at(name).Kind != DependencyKind::kInterface)
            {
                compiled.push_back(name);
            }
        }
        if (!compiled.empty())
        {
            std::ranges::sort(compiled, [&](const auto& lhs, const auto& rhs)
                              { return discoveryOrder.at(lhs) < discoveryOrder.at(rhs); });
            plan.CollectionGroups.emplace_back(std::move(compiled));
        }
    }
    return plan;
}

Status CompareDependencyProfiles(const DependencyGraph& graph, const DependencyPlan& plan,
                                 const std::map<std::string, nlohmann::json>& profiles, FailureReport& report)
{
    const Json* reference = nullptr;
    for (const auto& name : plan.Targets)
    {
        const auto target = graph.Targets.find(name);
        if (target == graph.Targets.end())
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_profile_unknown_target", name));
        }
        if (target->second.Kind == DependencyKind::kInterface)
        {
            continue; // Header-only target: no object library and no independent Profile.
        }
        if (target->second.Kind != DependencyKind::kModule && target->second.Kind != DependencyKind::kStatic)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_profile_invalid_target", name));
        }
        const auto found = profiles.find(name);
        if (found == profiles.end())
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_profile_missing", name));
        }
        const auto& profile = found->second;
        // This shape check is defensive; provenance and full frozen-schema
        // validation remain mandatory caller responsibilities.
        if (!profile.is_object() || !profile.contains("schema") || profile.at("schema") != 1 ||
            !profile.contains("policy") || !profile.at("policy").is_string() || profile.at("policy") != "strict-v1" ||
            !profile.contains("target") || !profile.at("target").is_object() || !profile.contains("compiler") ||
            !profile.at("compiler").is_object() || !profile.contains("language") ||
            !profile.at("language").is_object() || !profile.contains("stdlib") || !profile.at("stdlib").is_object() ||
            !profile.contains("runtime") || !profile.at("runtime").is_object())
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_profile_invalid_shape", name));
        }
        if (reference == nullptr)
        {
            reference = &profile;
            continue;
        }
        const auto differences = Json::diff(*reference, profile);
        if (!differences.empty())
        {
            const auto path = differences.front().at("path").get<std::string>();
            return std::unexpected(
                report.Add(ToolError::kInvalidProfile, "dependency_profile_mismatch", name, {}, std::nullopt, path));
        }
    }
    if (reference == nullptr)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_profile_no_compiled_targets"));
    }
    return {};
}

Result<std::vector<CollectionRequest>> ResolveCollectionGroup(const DependencyGraph& graph, const DependencyPlan& plan,
                                                              const CollectionRequest& root, FailureReport& report,
                                                              CollectionOutputWait wait)
{
    for (const auto& cycle : plan.TechnicalCycles)
    {
        if (std::ranges::find(cycle, root.TargetName) == cycle.end())
        {
            continue;
        }
        std::vector<CollectionRequest> result;
        for (const auto& name : cycle)
        {
            const auto& target = graph.Targets.at(name);
            const auto request = ReadCollectionRequest(target.RequestFile, report, wait);
            if (!request)
            {
                return std::unexpected(request.error());
            }
            if (request->TargetName != name || request->SourceRoot != root.SourceRoot ||
                request->BuildRoot != root.BuildRoot || request->Configuration != root.Configuration ||
                request->HostPlatform != root.HostPlatform || request->TargetPlatform != root.TargetPlatform ||
                request->LinkGraph != root.LinkGraph || request->SchemaFile != root.SchemaFile ||
                (name == root.TargetName && *request != root))
            {
                return std::unexpected(report.Add(ToolError::kInvalidRequest, "group_request_identity", name));
            }
            for (const auto& previous : result)
            {
                if (request->OutputDirectory == previous.OutputDirectory || request->Library == previous.Library ||
                    request->ObjectManifest == previous.ObjectManifest)
                {
                    return std::unexpected(report.Add(ToolError::kInvalidRequest, "group_member_overlap", name));
                }
            }
            result.push_back(*request);
        }
        std::ranges::sort(result, {}, &CollectionRequest::TargetName);
        return result;
    }
    return std::vector<CollectionRequest>{root};
}

Status VerifyDependencyProfiles(const DependencyGraph& graph, const DependencyPlan& plan,
                                const CollectionRequest& rootRequest, const nlohmann::json& rootProfile,
                                FailureReport& report)
{
    return VerifyCollectionDependencies(graph, plan, rootRequest, {{rootRequest.TargetName, rootProfile}}, report);
}
Status VerifyCollectionDependencies(const DependencyGraph& graph, const DependencyPlan& plan,
                                    const CollectionRequest& rootRequest,
                                    std::map<std::string, nlohmann::json> profiles, FailureReport& report)
{
    for (const auto& name : plan.Targets)
    {
        const auto target = graph.Targets.find(name);
        if (target == graph.Targets.end())
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_missing_target", name));
        }
        if (profiles.contains(name) ||
            (target->second.Kind != DependencyKind::kStatic && target->second.Kind != DependencyKind::kModule))
        {
            continue; // The current group uses its in-progress Profile, never Guard itself.
        }
        if (target->second.RequestFile.empty())
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_request_missing", name));
        }
        const auto dependency = ReadCollectionRequest(target->second.RequestFile, report);
        if (!dependency)
        {
            return std::unexpected(dependency.error());
        }
        if (dependency->TargetName != name || dependency->SourceRoot != rootRequest.SourceRoot ||
            dependency->BuildRoot != rootRequest.BuildRoot || dependency->Configuration != rootRequest.Configuration ||
            dependency->HostPlatform != rootRequest.HostPlatform ||
            dependency->TargetPlatform != rootRequest.TargetPlatform ||
            dependency->OutputDirectory == rootRequest.OutputDirectory || dependency->Library == rootRequest.Library)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_request_identity", name));
        }
        // Guard verifies the publisher's tool identity, all compiler receipts,
        // source/header snapshot, library bytes, profile and completion hashes.
        const auto guarded = GuardCollection(*dependency, report);
        if (!guarded)
        {
            return std::unexpected(guarded.error());
        }
        if (*guarded != GuardDisposition::kCurrent)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_completion_not_current", name));
        }
        const auto bytes = ReadFile(dependency->OutputDirectory / "profile.json", "dependency_profile_read", report);
        if (!bytes)
        {
            return std::unexpected(bytes.error());
        }
        auto decoded = ParseUnique(*bytes);
        if (!decoded)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "dependency_profile_parse", name));
        }
        profiles.emplace(name, std::move(*decoded));
    }
    return CompareDependencyProfiles(graph, plan, profiles, report);
}
} // namespace pg::abi
