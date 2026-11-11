#include "BuildLayout.h"
#include "PathEncoding.h"
#include "PluginDiscovery.h"
#include "PluginJson.h"

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{
int RunLayout(const std::vector<std::filesystem::path>& arguments)
{
    using pg::build::plugin::PathToUtf8;
    std::map<std::string, std::filesystem::path> options;
    for (std::size_t index = 2; index < arguments.size(); index += 2)
    {
        const auto key = PathToUtf8(arguments.at(index));
        if (index + 1 >= arguments.size() || !options.emplace(key, arguments.at(index + 1)).second)
        {
            std::cerr << "Missing value or duplicate layout option: " << key << '\n';
            return 2;
        }
    }
    for (const auto* key :
         {"--engine-source", "--engine-build", "--project-source", "--project-build", "--mode", "--config"})
    {
        if (!options.contains(key) || options.at(key).empty())
        {
            std::cerr << "Missing layout option: " << key << '\n';
            return 2;
        }
    }
    if (options.size() != 6)
    {
        std::cerr << "Unknown layout CLI option\n";
        return 2;
    }
    const auto result = pg::build::layout::Resolve({
        .EngineSource = options.at("--engine-source"),
        .EngineBuild = options.at("--engine-build"),
        .ProjectSource = options.at("--project-source"),
        .ProjectBuild = options.at("--project-build"),
        .Mode = PathToUtf8(options.at("--mode")),
        .Config = PathToUtf8(options.at("--config")),
    });
    if (!result)
    {
        std::cerr << result.error() << '\n';
        return 1;
    }
    if (const auto written = pg::build::layout::WriteState(*result); !written)
    {
        std::cerr << written.error() << '\n';
        return 1;
    }
    std::cout << pg::build::layout::ToJson(*result) << '\n';
    return 0;
}
} // namespace
#ifdef _WIN32
// Windows CRT 固定要求此入口名称及外部链接，不能改名或设为 static。
// NOLINTNEXTLINE(readability-identifier-naming,misc-use-internal-linkage)
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    using pg::build::plugin::PathToUtf8;
    std::vector<std::filesystem::path> arguments;
    for (const auto* argument : std::span(argv, static_cast<std::size_t>(argc)))
    {
        arguments.emplace_back(argument);
    }
    if (argc >= 2 && PathToUtf8(arguments.at(1)) == "layout")
    {
        return RunLayout(arguments);
    }
    if (argc < 4 || PathToUtf8(arguments.at(1)) != "discover")
    {
        std::cerr << "Usage: PanguBuildTool discover --engine-root <dir> [--project-root <dir>]\n";
        return 2;
    }

    std::optional<std::filesystem::path> engineRoot;
    std::optional<std::filesystem::path> projectRoot;
    for (int index = 2; index < argc; index += 2)
    {
        if (index + 1 >= argc)
        {
            std::cerr << "Missing value for CLI option: " << PathToUtf8(arguments.at(index)) << '\n';
            return 2;
        }
        const auto option = PathToUtf8(arguments.at(index));
        if (option == "--engine-root" && !engineRoot)
        {
            engineRoot.emplace(arguments.at(index + 1));
        }
        else if (option == "--project-root" && !projectRoot)
        {
            projectRoot.emplace(arguments.at(index + 1));
        }
        else
        {
            std::cerr << "Unknown or duplicate CLI option: " << option << '\n';
            return 2;
        }
    }
    if (!engineRoot || engineRoot->empty())
    {
        std::cerr << "Missing required --engine-root\n";
        return 2;
    }

    const auto result = pg::build::plugin::DiscoverPlugins(*engineRoot, projectRoot);
    if (!result)
    {
        std::cerr << result.error() << '\n';
        return 1;
    }
    std::cout << pg::build::plugin::PluginsToJson(*result) << '\n';
    return 0;
}
