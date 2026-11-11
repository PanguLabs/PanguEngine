#include "Driver/RunGeneration.h"
#include "Attr/Scan.h"
#include "CodeGen/Generate.h"
#include "Driver/Diagnostic.h"
#include "Driver/GenerationRequest.h"
#include "MetaIR/Model.h"
#include "Output/Publish.h"
#include <expected>
#include <filesystem>
#include <llvm/Support/FileSystem.h>
#include <string>
#include <utility>
#include <vector>

namespace pg::meta::tool
{
ToolResult<void> detail::RunGenerationFromFile(const GenerationRequest& request, const std::filesystem::path& source)
{
    ir::Model model;
    std::vector<std::filesystem::path> dependencies;
    if (request.Kind == TaskKind::kScan)
    {
        auto scan = Scan(request);
        if (!scan)
        {
            return std::unexpected(scan.error());
        }
        model = std::move(scan->Model);
        dependencies = std::move(scan->Dependencies);
    }
    const auto files = Generate(request, model);
    if (!files)
    {
        return std::unexpected(files.error());
    }
    if (!source.empty())
    {
        dependencies.push_back(source);
    }
    const auto executable = llvm::sys::fs::getMainExecutable(nullptr, nullptr);
    if (executable.empty())
    {
        return std::unexpected(Diagnostics{{.Code = "invalid_context", .Message = "无法确定当前生成工具路径"}});
    }
    dependencies.emplace_back(std::u8string(executable.begin(), executable.end()));
    return Publish(request, *files, dependencies);
}
ToolResult<void> RunGeneration(const GenerationRequest& request) { return detail::RunGenerationFromFile(request, {}); }
} // namespace pg::meta::tool
