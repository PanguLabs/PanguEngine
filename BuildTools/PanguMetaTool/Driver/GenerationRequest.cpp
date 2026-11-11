#include "Driver/GenerationRequest.h"
#include "Driver/Diagnostic.h"
#include "Driver/InputOwnership.h"
#include <algorithm>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <ios>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace pg::meta::tool
{
namespace
{
using Json = nlohmann::json;
using Path = std::filesystem::path;

std::unexpected<Diagnostics> Failure(std::string code, std::string message, const Path& path = {})
{
    const auto utf8 = path.generic_u8string();
    return std::unexpected(Diagnostics{
        {
            .Code = std::move(code),
            .Message = std::move(message),
            .Source = {.File = std::string(utf8.begin(), utf8.end()), .Line = 0, .Column = 0},
        },
    });
}

bool Shape(const Json& value, std::initializer_list<std::pair<std::string_view, Json::value_t>> members)
{
    if (!value.is_object() || value.size() != members.size())
    {
        return false;
    }
    return std::ranges::all_of(members,
                               [&](const auto& specification)
                               {
                                   const auto& [name, type] = specification;
                                   const auto member = value.find(name);
                                   if (member == value.end() || member->type() != type)
                                   {
                                       return false;
                                   }
                                   return type != Json::value_t::string ||
                                          !member->template get_ref<const std::string&>().contains('\0');
                               });
}

const Json& Member(const Json& value, std::string_view name)
{
    // 调用前 Shape 已证明字段存在及类型正确。
    return *value.find(name);
}

const std::string& Text(const Json& value, std::string_view name)
{
    // 仅对已通过 Shape 校验的对象调用。
    return value.find(name)->get_ref<const std::string&>();
}

Path NativePath(const std::string& text) { return {std::u8string(text.begin(), text.end())}; }

bool Component(std::string_view value)
{
    return !value.empty() && value != "." && value != ".." && value.find_first_of("/\\:") == std::string_view::npos;
}

bool RelativePath(const Path& path)
{
    if (path.empty() || path.has_root_path())
    {
        return false;
    }
    return std::ranges::all_of(path, [](const auto& part) { return part != ".." && part != "." && !part.empty(); });
}

ToolResult<Path> Canonical(const Path& path)
{
    std::error_code error;
    auto result = std::filesystem::weakly_canonical(path, error);
    if (error)
    {
        return Failure("invalid_request", "无法规范化文件路径: " + std::to_string(error.value()), path);
    }
    return result;
}

ToolResult<bool> SamePath(const Path& left, const Path& right)
{
    const auto a = Canonical(left);
    if (!a)
    {
        return std::unexpected(a.error());
    }
    const auto b = Canonical(right);
    if (!b)
    {
        return std::unexpected(b.error());
    }
    if (*a == *b)
    {
        return true;
    }
    std::error_code error;
    // prospective 输出尚不存在时不能调用 equivalent；不同标准库对此并不统一返回 ENOENT。
    const bool leftExists = std::filesystem::exists(*a, error);
    if (error)
    {
        return Failure("invalid_request", "无法检查文件身份: " + std::to_string(error.value()), left);
    }
    const bool rightExists = std::filesystem::exists(*b, error);
    if (error)
    {
        return Failure("invalid_request", "无法检查文件身份: " + std::to_string(error.value()), right);
    }
    if (!leftExists || !rightExists)
    {
        return false;
    }
    const bool equivalent = std::filesystem::equivalent(*a, *b, error);
    if (error && error != std::errc::no_such_file_or_directory)
    {
        return Failure("invalid_request", "无法比较文件身份: " + std::to_string(error.value()), left);
    }
    return equivalent;
}

ToolResult<void> ValidateOutputs(const GenerationRequest& request)
{
    const auto& build = request.Owner == SourceOwner::kEngine ? request.EngineBuild : request.ProjectBuild;
    const auto expected = build / "Generated" / "PanguMeta" / request.Config / request.Target;
    auto sameRoot = SamePath(request.OutputRoot, expected);
    if (!sameRoot)
    {
        return std::unexpected(sameRoot.error());
    }
    if (!*sameRoot)
    {
        return Failure("invalid_request", "output_root 必须是所属 BuildRoot 的 Generated/PanguMeta/Config/Target",
                       request.OutputRoot);
    }
    std::vector<Path> outputs;
    const auto canonicalRoot = Canonical(request.OutputRoot);
    if (!canonicalRoot)
    {
        return std::unexpected(canonicalRoot.error());
    }
    for (const auto& output : request.Outputs)
    {
        if (!RelativePath(output))
        {
            return Failure("invalid_request", "outputs 必须是不含逃逸部分的相对文件路径", output);
        }
        if (output.filename() == "Meta.outputs.json" || output.filename() == "Meta.d" ||
            output.filename() == "Meta.stamp")
        {
            return Failure("output_collision", "outputs 与批次状态保留名冲突", output);
        }
        const auto absolute = request.OutputRoot / output;
        const auto canonicalOutput = Canonical(absolute);
        if (!canonicalOutput)
        {
            return std::unexpected(canonicalOutput.error());
        }
        const auto relative = canonicalOutput->lexically_relative(*canonicalRoot);
        if (!RelativePath(relative))
        {
            return Failure("output_collision", "outputs 的真实文件路径逃逸生成根", output);
        }
        for (const auto* state : {"Meta.outputs.json", "Meta.d", "Meta.stamp"})
        {
            const auto same = SamePath(absolute, request.OutputRoot / state);
            if (!same)
            {
                return std::unexpected(same.error());
            }
            if (*same)
            {
                return Failure("output_collision", "outputs 的真实文件与批次状态保留文件冲突", output);
            }
        }
        for (const auto& previous : outputs)
        {
            const auto same = SamePath(absolute, previous);
            if (!same)
            {
                return std::unexpected(same.error());
            }
            if (*same)
            {
                return Failure("output_collision", "outputs 中存在重复文件或文件别名", output);
            }
        }
        outputs.push_back(*canonicalOutput);
    }
    return {};
}

ToolResult<void> ReadModule(const Json& value, GenerationRequest& request)
{
    if (request.Kind == TaskKind::kCatalog)
    {
        if (!value.empty())
        {
            return Failure("invalid_request", "catalog 不使用 module，必须传空对象");
        }
        return {};
    }
    if (!Shape(value, {
                          {"id_symbol", Json::value_t::string},
                          {"identity_header", Json::value_t::string},
                          {"query_symbol", Json::value_t::string},
                          {"query_header", Json::value_t::string},
                          {"export_macro", Json::value_t::string},
                      }))
    {
        return Failure("invalid_request", "module 字段或类型无效");
    }
    request.Module = {
        .IdSymbol = Text(value, "id_symbol"),
        .IdentityHeader = Text(value, "identity_header"),
        .QuerySymbol = Text(value, "query_symbol"),
        .QueryHeader = Text(value, "query_header"),
        .ExportMacro = Text(value, "export_macro"),
    };
    if (request.Module.IdSymbol.empty() || request.Module.IdentityHeader.empty() ||
        request.Module.QuerySymbol.empty() || request.Module.QueryHeader.empty())
    {
        return Failure("invalid_request", "module 缺少符号或头文件");
    }
    return {};
}

ToolResult<void> ReadInputs(const Json& value, GenerationRequest& request)
{
    for (const auto& header : Member(value, "headers"))
    {
        if (!Shape(header, {
                               {"path", Json::value_t::string},
                               {"root", Json::value_t::string},
                               {"prefix", Json::value_t::string},
                               {"visibility", Json::value_t::string},
                               {"typed_access", Json::value_t::boolean},
                               {"description", Json::value_t::boolean},
                           }))
        {
            return Failure("invalid_request", "header 字段或类型无效");
        }
        const auto& visibility = Text(header, "visibility");
        if (visibility != "public" && visibility != "private")
        {
            return Failure("invalid_request", "header visibility 必须是 public 或 private");
        }
        HeaderInput input{
            .Path = NativePath(Text(header, "path")),
            .Root = NativePath(Text(header, "root")),
            .Prefix = Text(header, "prefix"),
            .Visibility = visibility == "public" ? HeaderVisibility::kPublic : HeaderVisibility::kPrivate,
            .TypedAccess = Member(header, "typed_access").get<bool>(),
            .Description = Member(header, "description").get<bool>(),
        };
        if (!input.Path.is_absolute() || !input.Root.is_absolute() ||
            (!input.Prefix.empty() && !RelativePath(NativePath(input.Prefix))))
        {
            return Failure("invalid_request", "header 路径必须绝对，prefix 必须相对");
        }
        request.Headers.push_back(std::move(input));
    }
    for (const auto& entry : Member(value, "ownership"))
    {
        if (!Shape(entry, {{"path", Json::value_t::string}, {"target", Json::value_t::string}}))
        {
            return Failure("invalid_request", "ownership 字段或类型无效");
        }
        OwnershipEntry input{.Path = NativePath(Text(entry, "path")), .Target = Text(entry, "target")};
        if (!input.Path.is_absolute() || !Component(input.Target))
        {
            return Failure("invalid_request", "ownership 路径或目标无效");
        }
        request.Ownership.push_back(std::move(input));
    }
    const auto ownership = InputOwnership::Create(request.Ownership);
    if (!ownership)
    {
        return std::unexpected(ownership.error());
    }
    const auto& context = Member(value, "context");
    if (request.Kind != TaskKind::kScan)
    {
        if (!request.Headers.empty() || !context.empty())
        {
            return Failure("invalid_request", "query/catalog 不使用 headers/context，必须传空数组/对象");
        }
        return {};
    }
    if (!Shape(context, {
                            {"database", Json::value_t::string},
                            {"source", Json::value_t::string},
                            {"object", Json::value_t::string},
                            {"resource_dir", Json::value_t::string},
                        }))
    {
        return Failure("invalid_request", "context 字段或类型无效");
    }
    request.Context = {
        .Database = NativePath(Text(context, "database")),
        .Source = NativePath(Text(context, "source")),
        .Object = NativePath(Text(context, "object")),
        .ResourceDir = NativePath(Text(context, "resource_dir")),
    };
    if (!request.Context.Database.is_absolute() || !request.Context.Source.is_absolute() ||
        !request.Context.ResourceDir.is_absolute() ||
        (!request.Context.Object.empty() && !request.Context.Object.is_absolute()))
    {
        return Failure("invalid_context", "context 路径必须为绝对路径，object 可为空");
    }
    return {};
}

ToolResult<GenerationRequest> Decode(const Json& value)
{
    if (!Shape(value, {
                          {"version", Json::value_t::number_unsigned},
                          {"kind", Json::value_t::string},
                          {"target", Json::value_t::string},
                          {"owner", Json::value_t::string},
                          {"config", Json::value_t::string},
                          {"engine_source", Json::value_t::string},
                          {"engine_build", Json::value_t::string},
                          {"project_source", Json::value_t::string},
                          {"project_build", Json::value_t::string},
                          {"output_root", Json::value_t::string},
                          {"module", Json::value_t::object},
                          {"headers", Json::value_t::array},
                          {"ownership", Json::value_t::array},
                          {"context", Json::value_t::object},
                          {"catalog", Json::value_t::array},
                          {"outputs", Json::value_t::array},
                      }))
    {
        return Failure("invalid_request", "请求缺少必需字段、含未知字段或字段类型错误");
    }
    if (Member(value, "version").get<std::uint64_t>() != 1)
    {
        return Failure("invalid_request", "仅支持请求 version=1");
    }
    const auto& kind = Text(value, "kind");
    const auto& owner = Text(value, "owner");
    if ((kind != "scan" && kind != "query" && kind != "catalog") || (owner != "engine" && owner != "project"))
    {
        return Failure("invalid_request", "kind 或 owner 无效");
    }
    GenerationRequest request;
    request.Version = 1;
    if (kind == "query")
    {
        request.Kind = TaskKind::kQuery;
    }
    else if (kind == "catalog")
    {
        request.Kind = TaskKind::kCatalog;
    }
    request.Owner = owner == "engine" ? SourceOwner::kEngine : SourceOwner::kProject;
    request.Target = Text(value, "target");
    request.Config = Text(value, "config");
    request.EngineSource = NativePath(Text(value, "engine_source"));
    request.EngineBuild = NativePath(Text(value, "engine_build"));
    request.ProjectSource = NativePath(Text(value, "project_source"));
    request.ProjectBuild = NativePath(Text(value, "project_build"));
    request.OutputRoot = NativePath(Text(value, "output_root"));
    if (!Component(request.Target) || !Component(request.Config) || !request.EngineSource.is_absolute() ||
        !request.EngineBuild.is_absolute() || !request.OutputRoot.is_absolute() ||
        (!request.ProjectSource.empty() && !request.ProjectSource.is_absolute()) ||
        (!request.ProjectBuild.empty() && !request.ProjectBuild.is_absolute()) ||
        (request.Owner == SourceOwner::kProject && (request.ProjectSource.empty() || request.ProjectBuild.empty())))
    {
        return Failure("invalid_request", "目标、配置或源码/构建根路径无效");
    }
    const auto module = ReadModule(Member(value, "module"), request);
    if (!module)
    {
        return std::unexpected(module.error());
    }
    const auto inputs = ReadInputs(value, request);
    if (!inputs)
    {
        return std::unexpected(inputs.error());
    }
    for (const auto& entry : Member(value, "catalog"))
    {
        if (!Shape(entry, {
                              {"target", Json::value_t::string},
                              {"query_symbol", Json::value_t::string},
                              {"query_header", Json::value_t::string},
                          }))
        {
            return Failure("invalid_request", "catalog 字段或类型无效");
        }
        if (!Component(Text(entry, "target")) || Text(entry, "query_symbol").empty() ||
            Text(entry, "query_header").empty())
        {
            return Failure("invalid_request", "catalog 缺少目标或 Query 符号/头文件");
        }
        request.Catalog.push_back({
            .Target = Text(entry, "target"),
            .QuerySymbol = Text(entry, "query_symbol"),
            .QueryHeader = Text(entry, "query_header"),
        });
    }
    if (request.Kind != TaskKind::kCatalog && !request.Catalog.empty())
    {
        return Failure("invalid_request", "scan/query 不使用 catalog，必须传空数组");
    }
    for (const auto& output : Member(value, "outputs"))
    {
        if (!output.is_string() || output.get_ref<const std::string&>().contains('\0'))
        {
            return Failure("invalid_request", "outputs 必须是路径字符串数组");
        }
        request.Outputs.push_back(NativePath(output.get_ref<const std::string&>()));
    }
    const auto outputs = ValidateOutputs(request);
    if (!outputs)
    {
        return std::unexpected(outputs.error());
    }
    return request;
}
} // namespace

ToolResult<GenerationRequest> ReadRequest(const Path& path)
{
    if (!path.is_absolute())
    {
        return Failure("invalid_request", "request 路径必须为绝对路径", path);
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return Failure("invalid_request", "无法读取请求文件", path);
    }
    const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (stream.bad())
    {
        return Failure("invalid_request", "读取请求文件失败", path);
    }
    bool duplicate = false;
    std::vector<std::set<std::string>> keys;
    const auto callback = [&](int, Json::parse_event_t event, Json& parsed)
    {
        if (event == Json::parse_event_t::object_start)
        {
            keys.emplace_back();
        }
        else if (event == Json::parse_event_t::key && parsed.is_string())
        {
            duplicate = !keys.back().insert(parsed.get_ref<const std::string&>()).second || duplicate;
        }
        else if (event == Json::parse_event_t::object_end)
        {
            keys.pop_back();
        }
        return true;
    };
    const auto value = Json::parse(text, callback, false);
    if (value.is_discarded() || duplicate)
    {
        return Failure("invalid_request", duplicate ? "请求 JSON 含重复键" : "请求不是合法 UTF-8 JSON", path);
    }
    auto result = Decode(value);
    if (!result)
    {
        const auto utf8 = path.generic_u8string();
        for (auto& diagnostic : result.error())
        {
            if (diagnostic.Source.File.empty())
            {
                diagnostic.Source.File.assign(utf8.begin(), utf8.end());
            }
        }
    }
    return result;
}

} // namespace pg::meta::tool
