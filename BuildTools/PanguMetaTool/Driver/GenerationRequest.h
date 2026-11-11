#pragma once

#include "Driver/Diagnostic.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pg::meta::tool
{
enum class TaskKind : std::uint8_t
{
    kScan,
    kQuery,
    kCatalog,
};
enum class SourceOwner : std::uint8_t
{
    kEngine,
    kProject,
};
enum class HeaderVisibility : std::uint8_t
{
    kPublic,
    kPrivate,
};

struct HeaderInput
{
    std::filesystem::path Path, Root;
    std::string Prefix;
    HeaderVisibility Visibility{HeaderVisibility::kPublic};
    bool TypedAccess{};
    bool Description{};
};
struct OwnershipEntry
{
    std::filesystem::path Path;
    std::string Target;
};
struct ContextInput
{
    std::filesystem::path Database, Source, Object, ResourceDir;
};
struct ModuleBinding
{
    std::string IdSymbol, IdentityHeader, QuerySymbol, QueryHeader, ExportMacro;
};
struct CatalogEntry
{
    std::string Target, QuerySymbol, QueryHeader;
};
struct GenerationRequest
{
    std::uint32_t Version{};
    TaskKind Kind{TaskKind::kScan};
    std::string Target;
    SourceOwner Owner{SourceOwner::kEngine};
    std::string Config;
    std::filesystem::path EngineSource, EngineBuild, ProjectSource, ProjectBuild, OutputRoot;
    ModuleBinding Module;
    std::vector<HeaderInput> Headers;
    std::vector<OwnershipEntry> Ownership;
    ContextInput Context;
    std::vector<CatalogEntry> Catalog;
    std::vector<std::filesystem::path> Outputs;
};

[[nodiscard]] ToolResult<GenerationRequest> ReadRequest(const std::filesystem::path& path);
} // namespace pg::meta::tool
