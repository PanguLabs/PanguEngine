#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace pg::build::layout
{
struct Request
{
    std::filesystem::path EngineSource;
    std::filesystem::path EngineBuild;
    std::filesystem::path ProjectSource;
    std::filesystem::path ProjectBuild;
    std::string Mode;
    std::string Config;
};

struct Layout
{
    Request Roots;
    std::filesystem::path CurrentProject;
    std::filesystem::path CurrentContext;
};

[[nodiscard]] std::expected<Layout, std::string> Resolve(const Request& request);
[[nodiscard]] std::expected<void, std::string> WriteState(const Layout& layout);
[[nodiscard]] std::string ToJson(const Layout& layout);
} // namespace pg::build::layout
