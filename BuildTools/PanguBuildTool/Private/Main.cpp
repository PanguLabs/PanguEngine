#include "PathEncoding.h"
#include "PluginDiscovery.h"
#include "PluginJson.h"

#include <filesystem>
#include <iostream>
#include <optional>
#ifdef _WIN32
// Windows CRT 固定要求此入口名称及外部链接，不能改名或设为 static。
// NOLINTNEXTLINE(readability-identifier-naming,misc-use-internal-linkage)
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    using pg::build::plugin::PathToUtf8;
    if (argc < 4 || PathToUtf8(std::filesystem::path(argv[1])) != "discover")
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
            std::cerr << "Missing value for CLI option: " << PathToUtf8(std::filesystem::path(argv[index])) << '\n';
            return 2;
        }
        const auto option = PathToUtf8(std::filesystem::path(argv[index]));
        if (option == "--engine-root" && !engineRoot)
        {
            engineRoot.emplace(argv[index + 1]);
        }
        else if (option == "--project-root" && !projectRoot)
        {
            projectRoot.emplace(argv[index + 1]);
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
