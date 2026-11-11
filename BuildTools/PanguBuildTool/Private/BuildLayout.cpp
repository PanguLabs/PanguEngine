#include "BuildLayout.h"
#include "PathEncoding.h"

#include <array>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

namespace pg::build::layout
{
namespace
{
std::string Quote(std::string_view input)
{
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string output = "\"";
    for (const unsigned char value : input)
    {
        if (value == '"' || value == '\\')
        {
            output += '\\';
            output += static_cast<char>(value);
        }
        else if (value < 0x20)
        {
            output += "\\u00";
            output += kDigits.at(value / 16U);
            output += kDigits.at(value % 16U);
        }
        else
        {
            output += static_cast<char>(value);
        }
    }
    return output + '"';
}

bool Contains(const std::filesystem::path& parent, const std::filesystem::path& child)
{
    for (auto ancestor = child; !ancestor.empty(); ancestor = ancestor.parent_path())
    {
        std::error_code error;
        if (ancestor == parent || std::filesystem::equivalent(parent, ancestor, error))
        {
            return true;
        }
        if (ancestor == ancestor.parent_path())
        {
            break;
        }
    }
    return false;
}

std::expected<void, std::string> WriteIfChanged(const std::filesystem::path& path, const std::string& content)
{
    std::ifstream input(path, std::ios::binary);
    if (input)
    {
        const std::string previous{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (previous == content)
        {
            return {};
        }
    }
    std::ofstream output(path, std::ios::binary);
    output << content;
    output.close();
    if (!output)
    {
        return std::unexpected("Cannot write build context: " + plugin::PathToUtf8(path));
    }
    return {};
}
} // namespace

std::expected<Layout, std::string> Resolve(const Request& request)
{
    if (request.Mode != "Monolithic" && request.Mode != "Modular")
    {
        return std::unexpected("Build layout mode must be Monolithic|Modular");
    }
    if (request.Config.empty() ||
        request.Config.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
            std::string::npos)
    {
        return std::unexpected("Build layout requires a valid single configuration");
    }
    Request roots = request;
    const std::array paths{&roots.EngineSource, &roots.EngineBuild, &roots.ProjectSource, &roots.ProjectBuild};
    for (auto* path : paths)
    {
        if (!path->is_absolute())
        {
            return std::unexpected("Source/build root must be absolute: " + plugin::PathToUtf8(*path));
        }
        std::error_code error;
        *path = std::filesystem::weakly_canonical(*path, error);
        if (error)
        {
            return std::unexpected("Cannot normalize source/build root: " + error.message());
        }
    }
    for (const auto* source : {&roots.EngineSource, &roots.ProjectSource})
    {
        std::error_code error;
        if (!std::filesystem::is_directory(*source, error) || error)
        {
            return std::unexpected("Source root must exist: " + plugin::PathToUtf8(*source));
        }
        for (const auto* build : {&roots.EngineBuild, &roots.ProjectBuild})
        {
            // 正式 BuildRoot 可位于 checkout 的 Build 下，但不能覆盖源码或其祖先。
            if (Contains(*build, *source))
            {
                return std::unexpected("Invalid build root overlaps source root: " + plugin::PathToUtf8(*build));
            }
        }
    }
    for (const auto* build : {&roots.EngineBuild, &roots.ProjectBuild})
    {
        std::error_code error;
        const bool exists = std::filesystem::exists(*build, error);
        if (error || (exists && !std::filesystem::is_directory(*build, error)))
        {
            return std::unexpected("Invalid build root must be a directory: " + plugin::PathToUtf8(*build));
        }
    }
    const auto context = roots.EngineBuild / "Generated" / "BuildContext";
    return Layout{
        .Roots = roots,
        .CurrentProject = context / "CurrentProject.json",
        .CurrentContext = context / "CurrentContext.h",
    };
}

std::string ToJson(const Layout& layout)
{
    const auto path = [](const std::filesystem::path& value) { return Quote(plugin::PathToUtf8(value, true)); };
    const auto& roots = layout.Roots;
    return R"({"version":1,"engine_source":)" + path(roots.EngineSource) + R"(,"engine_build":)" +
           path(roots.EngineBuild) + R"(,"project_source":)" + path(roots.ProjectSource) + R"(,"project_build":)" +
           path(roots.ProjectBuild) + R"(,"mode":)" + Quote(roots.Mode) + R"(,"config":)" + Quote(roots.Config) +
           R"(,"current_project":)" + path(layout.CurrentProject) + R"(,"current_context":)" +
           path(layout.CurrentContext) + "}";
}

std::expected<void, std::string> WriteState(const Layout& layout)
{
    std::error_code error;
    std::filesystem::create_directories(layout.CurrentProject.parent_path(), error);
    if (error)
    {
        return std::unexpected("Cannot create build context directory: " + error.message());
    }
    const auto json = ToJson(layout);
    // 共用头属于原生编译依赖；Project 往返时必须先更新它，再登记当前状态。
    if (const auto result = WriteIfChanged(layout.CurrentContext, "#pragma once\n// Build context: " + json + "\n");
        !result)
    {
        return result;
    }
    return WriteIfChanged(layout.CurrentProject, json + '\n');
}
} // namespace pg::build::layout
