#pragma once

#include "ToolReport.h"
#include <Result.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pg::abi
{
enum class Platform : std::uint8_t
{
    kWindows,
    kLinux,
    kMacos,
    kAndroid,
    kIos,
};

enum class CommandLineStyle : std::uint8_t
{
    kWindows,
    kPosix,
};

// Private request v2. Paths and text own their storage; JSON paths are UTF-8.
// Library and the objects named by ObjectManifest may not exist before compilation.
struct CollectionRequest
{
    // Reader-owned identity; private request JSON remains schema 2.
    std::filesystem::path RequestFile;
    std::filesystem::path SourceRoot;
    std::filesystem::path BuildRoot;
    std::string TargetName;
    std::string Configuration;
    Platform HostPlatform{};
    Platform TargetPlatform{};
    pg::abi::CommandLineStyle CommandLineStyle{};
    std::filesystem::path Compiler;
    std::filesystem::path Database;
    std::filesystem::path ObjectManifest;
    std::filesystem::path Library;
    std::filesystem::path OutputDirectory;
    std::filesystem::path SchemaFile;
    std::filesystem::path SignatureHeader;
    std::filesystem::path LinkGraph;
    nlohmann::json PlatformContext;
    std::map<std::string, std::optional<std::string>> EnvironmentBaseline;

    bool operator==(const CollectionRequest&) const = default;
};

[[nodiscard]] Result<CollectionRequest> ReadCollectionRequest(const std::filesystem::path& path, FailureReport& report);
[[nodiscard]] std::vector<std::filesystem::path> CollectionOutputFiles(const CollectionRequest& request);
[[nodiscard]] std::vector<std::filesystem::path> CollectionOutputDirectories(const CollectionRequest& request);
[[nodiscard]] Status CheckCollectionInputPaths(const CollectionRequest& request,
                                               std::span<const std::filesystem::path> inputs, FailureReport& report);
} // namespace pg::abi
