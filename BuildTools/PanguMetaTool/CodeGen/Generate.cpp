#include "CodeGen/Generate.h"
#include "CodeGen/CatalogEmitter.h"
#include "CodeGen/ModuleEmitter.h"
#include "CodeGen/QueryEmitter.h"
#include "CodeGen/TypeEmitter.h"
#include "Driver/Diagnostic.h"
#include "Driver/GenerationRequest.h"
#include "Driver/OutputMapping.h"
#include "MetaIR/Model.h"
#include "Output/GeneratedFile.h"
#include <algorithm>
#include <expected>
#include <filesystem>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>
namespace pg::meta::tool
{
namespace
{
using Path = std::filesystem::path;
Path Native(std::string_view text) { return {std::u8string(text.begin(), text.end())}; }
bool IncludeName(std::string_view name)
{
    return !name.empty() && !Native(name).is_absolute() && name.find_first_of("\"\r\n\\") == std::string_view::npos;
}
bool CppName(std::string_view name)
{
    if (name.starts_with("::"))
    {
        name.remove_prefix(2);
    }
    while (!name.empty())
    {
        const auto end = name.find("::");
        const auto part = name.substr(0, end);
        if (part.empty() || (part.front() >= '0' && part.front() <= '9') ||
            !std::ranges::all_of(part,
                                 [](unsigned char byte)
                                 {
                                     return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                                            (byte >= '0' && byte <= '9') || byte == '_' || byte >= 128;
                                 }))
        {
            return false;
        }
        if (end == std::string_view::npos)
        {
            return true;
        }
        name.remove_prefix(end + 2);
    }
    return false;
}
} // namespace
ToolResult<std::vector<GeneratedFile>> Generate(const GenerationRequest& request, const ir::Model& model)
{
    const auto mapping = BuildOutputMapping(request);
    if (!mapping)
    {
        return std::unexpected(mapping.error());
    }
    if (request.Kind == TaskKind::kCatalog)
    {
        if (std::ranges::any_of(request.Catalog, [](const auto& entry)
                                { return !CppName(entry.QuerySymbol) || !IncludeName(entry.QueryHeader); }))
        {
            return std::unexpected(
                Diagnostics{{.Code = "invalid_request", .Message = "Catalog Query 符号或头文件无效"}});
        }
        return std::vector{EmitCatalog(request)};
    }
    if (!CppName(request.Module.QuerySymbol) || !IncludeName(request.Module.QueryHeader) ||
        (!request.Module.ExportMacro.empty() &&
         (!CppName(request.Module.ExportMacro) || request.Module.ExportMacro.contains(':'))))
    {
        return std::unexpected(
            Diagnostics{{.Code = "invalid_request", .Message = "Module Query 符号、头文件或导出宏无效"}});
    }
    if (request.Kind == TaskKind::kQuery)
    {
        return std::vector{EmitQuery(request)};
    }
    if (!IncludeName(request.Module.IdentityHeader) || !CppName(request.Module.IdSymbol))
    {
        return std::unexpected(Diagnostics{{.Code = "invalid_request", .Message = "缺少可包含的公共 Module 身份声明"}});
    }
    std::vector<HeaderEmission> headers;
    for (const auto& input : request.Headers)
    {
        auto stem = Native(input.Prefix) / input.Path.lexically_relative(input.Root);
        stem.replace_extension();
        HeaderEmission header;
        header.TypedAccess = input.TypedAccess;
        header.Description = input.Description;
        header.Declaration = Path(input.Visibility == HeaderVisibility::kPublic ? "Public" : "Private") / stem;
        header.Declaration += ".gen.h";
        header.Implementation = Path("Private") / stem;
        header.Implementation += ".gen.cpp";
        const auto include =
            input.Path.lexically_relative((request.OutputRoot / header.Declaration).parent_path()).generic_u8string();
        header.OriginalInclude.assign(include.begin(), include.end());
        if ((input.TypedAccess || input.Description) && !IncludeName(header.OriginalInclude))
        {
            return std::unexpected(
                Diagnostics{{.Code = "unsupported_declaration", .Message = "原始头无法表达为无歧义相对 include"}});
        }
        for (const auto& type : model.Types)
        {
            if (Native(type.Source.File) == input.Path)
            {
                header.Types.push_back(&type);
            }
        }
        headers.push_back(std::move(header));
    }
    for (const auto& type : model.Types)
    {
        if (std::ranges::none_of(headers, [&](const auto& header)
                                 { return std::ranges::find(header.Types, &type) != header.Types.end(); }))
        {
            return std::unexpected(
                Diagnostics{{.Code = "unregistered_input", .Message = "IR 类型没有对应登记头", .Source = type.Source}});
        }
    }
    std::vector<GeneratedFile> files;
    for (const auto& header : headers)
    {
        if (header.TypedAccess || header.Description)
        {
            auto emitted = EmitTypes(request, header);
            files.insert(files.end(), std::make_move_iterator(emitted.begin()), std::make_move_iterator(emitted.end()));
        }
    }
    if (std::ranges::any_of(headers, &HeaderEmission::Description))
    {
        auto emitted = EmitModule(request, headers);
        files.insert(files.end(), std::make_move_iterator(emitted.begin()), std::make_move_iterator(emitted.end()));
    }
    if (std::ranges::find(request.Outputs, Path("Private/ModuleQuery.gen.cpp")) != request.Outputs.end())
    {
        files.push_back(EmitQuery(request));
    }
    std::ranges::sort(files, {}, &GeneratedFile::RelativePath);
    return files;
}
} // namespace pg::meta::tool
