#include "Driver/OutputMapping.h"
#include "Driver/Diagnostic.h"
#include "Driver/GenerationRequest.h"
#include <algorithm>
#include <expected>
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace pg::meta::tool
{
namespace
{
using Path = std::filesystem::path;
std::unexpected<Diagnostics> Collision(std::string message)
{
    return std::unexpected(Diagnostics{{.Code = "output_collision", .Message = std::move(message)}});
}
bool Relative(const Path& path)
{
    return !path.empty() && !path.has_root_path() &&
           std::ranges::all_of(path, [](const auto& part) { return !part.empty() && part != "." && part != ".."; });
}
} // namespace
ToolResult<std::vector<Path>> BuildOutputMapping(const GenerationRequest& request)
{
    std::vector<Path> result;
    if (request.Kind == TaskKind::kQuery)
    {
        result.emplace_back("Private/ModuleQuery.gen.cpp");
    }
    else if (request.Kind == TaskKind::kCatalog)
    {
        result.emplace_back("Private/StaticModuleCatalog.gen.cpp");
    }
    else
    {
        if (std::ranges::find(request.Outputs, Path("Private/ModuleQuery.gen.cpp")) != request.Outputs.end())
        {
            if (request.Module.IdSymbol.empty() || request.Module.IdentityHeader.empty() ||
                request.Module.QuerySymbol.empty() || request.Module.QueryHeader.empty())
            {
                return Collision("scan 合并 Query 槽位需要完整 Module 绑定");
            }
            result.emplace_back("Private/ModuleQuery.gen.cpp");
        }
        const bool descriptions =
            std::ranges::any_of(request.Headers, [](const auto& input) { return input.Description; });
        if (descriptions)
        {
            result.emplace_back("Private/ModuleMeta.gen.h");
            result.emplace_back("Private/ModuleMeta.gen.cpp");
        }
        for (const auto& header : request.Headers)
        {
            const Path prefix(std::u8string(header.Prefix.begin(), header.Prefix.end()));
            const auto relative = header.Path.lexically_relative(header.Root);
            if (!header.Path.is_absolute() || !header.Root.is_absolute() || !Relative(relative) ||
                (!prefix.empty() && !Relative(prefix)))
            {
                return Collision("输入必须位于 root 内，prefix 不得逃逸");
            }
            auto stem = prefix / relative;
            stem.replace_extension();
            const auto declaration = Path(header.Visibility == HeaderVisibility::kPublic ? "Public" : "Private") / stem;
            const Path headerOutput(declaration.native() + Path(".gen.h").native());
            const Path sourceOutput((Path("Private") / stem).native() + Path(".gen.cpp").native());
            if (((header.TypedAccess || header.Description) && headerOutput == "Private/ModuleMeta.gen.h") ||
                (header.Description &&
                 (sourceOutput == "Private/ModuleMeta.gen.cpp" || sourceOutput == "Private/ModuleQuery.gen.cpp" ||
                  sourceOutput == "Private/StaticModuleCatalog.gen.cpp")))
            {
                return Collision("per-header 输出不得占用固定汇总槽位，可用 prefix 消歧");
            }
            if (header.TypedAccess || header.Description)
            {
                result.push_back(headerOutput);
            }
            if (header.Description)
            {
                result.push_back(sourceOutput);
            }
        }
    }
    std::set<Path> mapped;
    for (const auto& output : result)
    {
        if (!mapped.insert(output).second)
        {
            return Collision("输入映射互相冲突或占用固定汇总槽位");
        }
    }
    for (const auto& output : result)
    {
        for (auto parent = output.parent_path(); !parent.empty(); parent = parent.parent_path())
        {
            if (mapped.contains(parent))
            {
                return Collision("输出同时用作文件和父目录");
            }
        }
    }
    std::set<Path> declared;
    for (const auto& output : request.Outputs)
    {
        if (!Relative(output) || !declared.insert(output).second)
        {
            return Collision("outputs 含无效或重复路径");
        }
    }
    if (declared != mapped)
    {
        return Collision("outputs 必须精确声明本任务的完整映射集合");
    }
    return result;
}
} // namespace pg::meta::tool
