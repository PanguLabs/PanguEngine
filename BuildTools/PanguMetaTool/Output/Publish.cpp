#include "Output/Publish.h"
#include "Driver/Diagnostic.h"
#include "Driver/GenerationRequest.h"
#include "Output/GeneratedFile.h"
#include "Output/PublishInternal.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
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
using Path = std::filesystem::path;
using Paths = std::set<Path>;
std::unexpected<Diagnostics> Failed(std::string message)
{
    return std::unexpected(Diagnostics{{.Code = "publish_failed", .Message = std::move(message)}});
}
std::string Utf8(const Path& path)
{
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}
bool Relative(const Path& path)
{
    const auto text = path.generic_u8string();
    if (path.empty() || path.has_root_path() || text.contains(u8':') || text.contains(u8'\\') ||
        text.contains(u8'\0') || text.contains(u8'\n') || text.contains(u8'\r'))
    {
        return false;
    }
    return std::ranges::all_of(path, [](const Path& part) { return !part.empty() && part != "." && part != ".."; });
}
bool OutputPath(const Path& path)
{
    if (!Relative(path) || !IsPlatformOutputPath(path))
    {
        return false;
    }
    auto text = Utf8(*path.begin());
    for (auto& ch : text)
    {
        if (ch >= 'A' && ch <= 'Z')
        {
            ch = static_cast<char>(ch + ('a' - 'A'));
        }
    }
    return text != "meta.outputs.json" && text != "meta.d" && text != "meta.stamp" && !text.starts_with(".meta-");
}
// 生成树不接受目录或文件链接。每次破坏性操作前重复检查，失败时保留 ownership。
ToolResult<void> CheckPath(const Path& path, bool directory)
{
    std::error_code error;
    Path current;
    for (const auto& component : path)
    {
        current /= component;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error == std::errc::no_such_file_or_directory)
        {
            error.clear();
            continue;
        }
        if (error)
        {
            return Failed("无法检查输出路径: " + Utf8(current));
        }
        if (std::filesystem::is_symlink(status))
        {
            return Failed("输出路径不得经过链接: " + Utf8(current));
        }
        if (!std::filesystem::exists(status))
        {
            continue;
        }
        const bool wantDirectory = current != path || directory;
        if (wantDirectory ? !std::filesystem::is_directory(status) : !std::filesystem::is_regular_file(status))
        {
            return Failed("输出路径类型不匹配: " + Utf8(current));
        }
    }
    return {};
}
ToolResult<std::string> Read(const Path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return Failed("无法读取文件: " + Utf8(path));
    }
    std::string result{std::istreambuf_iterator<char>(stream), {}};
    if (stream.bad())
    {
        return Failed("读取文件失败: " + Utf8(path));
    }
    return result;
}
ToolResult<Paths> ReadIndex(const Path& path)
{
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error)
    {
        return Failed("无法检查 outputs index");
    }
    if (!exists)
    {
        return Paths{};
    }
    const auto text = Read(path);
    if (!text)
    {
        return std::unexpected(text.error());
    }
    const auto json = nlohmann::json::parse(*text, nullptr, false);
    if (!json.is_object() || json.size() != 2 || !json.contains("version") || !json.at("version").is_number_integer() ||
        json.at("version") != 1 || !json.contains("outputs") || !json.at("outputs").is_array())
    {
        return Failed("outputs index 损坏或版本不支持");
    }
    Paths result;
    for (const auto& item : json.at("outputs"))
    {
        if (!item.is_string())
        {
            return Failed("outputs index 路径类型无效");
        }
        const auto& name = item.get_ref<const std::string&>();
        const Path relative(std::u8string(name.begin(), name.end()));
        if (!OutputPath(relative) || !result.insert(relative).second)
        {
            return Failed("outputs index 含无效或重复路径");
        }
    }
    return result;
}
std::string Index(const Paths& paths)
{
    auto outputs = nlohmann::json::array();
    for (const auto& path : paths)
    {
        outputs.push_back(Utf8(path));
    }
    return nlohmann::json{{"version", 1}, {"outputs", outputs}}.dump() + '\n';
}
ToolResult<std::string> EscapeDependency(const Path& path)
{
    std::string result;
    for (const char ch : Utf8(path))
    {
        switch (ch)
        {
        case '\0':
        case '\n':
        case '\r':
            return Failed("depfile 路径含不可表示的换行或空字符");
        case '$':
            result += "$$";
            break;
        case ' ':
        case '\t':
        case '#':
        case ':':
        case '\\':
            result += '\\';
            result += ch;
            break;
        default:
            result += ch;
            break;
        }
    }
    return result;
}
ToolResult<std::string> Depfile(const Path& stamp, std::span<const Path> dependencies)
{
    const auto target = EscapeDependency(stamp);
    if (!target)
    {
        return std::unexpected(target.error());
    }
    auto result = *target + ':';
    for (const auto& dependency : dependencies)
    {
        const auto escaped = EscapeDependency(dependency);
        if (!escaped)
        {
            return std::unexpected(escaped.error());
        }
        result += ' ';
        result += *escaped;
    }
    return result + '\n';
}
class Staging
{
public:
    explicit Staging(Path root)
        : Root(std::move(root))
    {
    }
    Staging(const Staging&) = delete;
    Staging& operator=(const Staging&) = delete;
    Staging(Staging&&) = delete;
    Staging& operator=(Staging&&) = delete;
    ~Staging()
    {
        // 只删除本轮明确创建的文件；绝不枚举或猜测遗留作业的路径。
        std::error_code error;
        for (const auto& file : Files)
        {
            std::filesystem::remove(file, error);
        }
        if (Owned)
        {
            std::filesystem::remove(Root, error);
        }
    }
    [[nodiscard]] ToolResult<void> Create()
    {
        std::error_code error;
        if (!std::filesystem::create_directory(Root, error))
        {
            return Failed("无法独占 staging 目录");
        }
        Owned = true;
        return {};
    }
    [[nodiscard]] ToolResult<Path> Add(std::string_view content)
    {
        auto path = Root / std::to_string(Files.size());
        const auto written = WriteExclusive(path, content);
        if (!written)
        {
            return std::unexpected(written.error());
        }
        Files.push_back(path);
        return path;
    }

private:
    Path Root;
    std::vector<Path> Files;
    bool Owned{};
};
ToolResult<void> ValidateStates(const Path& root)
{
    std::vector<Path> states;
    std::error_code error;
    for (const auto* name : {"Meta.outputs.json", "Meta.d", "Meta.stamp"})
    {
        const auto path = root / name;
        const auto checked = CheckPath(path, false);
        if (!checked)
        {
            return checked;
        }
        const bool exists = std::filesystem::exists(path, error);
        if (error)
        {
            return Failed("无法检查状态文件存在性");
        }
        if (!exists)
        {
            continue;
        }
        for (const auto& other : states)
        {
            const bool same = std::filesystem::equivalent(path, other, error);
            if (error && error != std::errc::no_such_file_or_directory)
            {
                return Failed("无法检查状态文件身份");
            }
            error.clear();
            if (same)
            {
                return Failed("状态文件互相别名");
            }
        }
        states.push_back(path);
    }
    return {};
}
} // namespace

ToolResult<void> Publish(const GenerationRequest& request, std::span<const GeneratedFile> files,
                         std::span<const Path> dependencies, const PublishOptions& options)
{
    const auto& build = request.Owner == SourceOwner::kEngine ? request.EngineBuild : request.ProjectBuild;
    const Path config(std::u8string(request.Config.begin(), request.Config.end()));
    const Path target(std::u8string(request.Target.begin(), request.Target.end()));
    if (!build.is_absolute() || !Relative(config) || config.has_parent_path() || !Relative(target) ||
        target.has_parent_path() || request.OutputRoot != build / "Generated/PanguMeta" / config / target ||
        request.Version != 1)
    {
        return Failed("生成根必须等于所属 BuildRoot/Generated/PanguMeta/Config/Target");
    }
    const auto& root = request.OutputRoot;
    const auto rootCheck = CheckPath(root, true);
    if (!rootCheck)
    {
        return rootCheck;
    }
    const auto states = ValidateStates(root);
    if (!states)
    {
        return states;
    }
    Paths requested;
    for (const auto& relative : request.Outputs)
    {
        if (!OutputPath(relative) || !requested.insert(relative).second)
        {
            return Failed("outputs 路径无效或重复");
        }
    }
    Paths generated;
    for (const auto& file : files)
    {
        if (!generated.insert(file.RelativePath).second)
        {
            return Failed("候选路径重复");
        }
    }
    if (requested != generated)
    {
        return Failed("候选集合必须精确匹配 outputs");
    }
    const auto old = ReadIndex(root / "Meta.outputs.json");
    if (!old)
    {
        return std::unexpected(old.error());
    }
    auto owned = *old;
    owned.insert(requested.begin(), requested.end());
    for (const auto& relative : owned)
    {
        const auto checked = CheckPath(root / relative, false);
        if (!checked)
        {
            return checked;
        }
    }
    const auto depfile = Depfile(root / "Meta.stamp", dependencies);
    if (!depfile)
    {
        return std::unexpected(depfile.error());
    }
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error)
    {
        return Failed("无法创建生成根");
    }
    const auto mapping = ValidatePublishPaths(root, {owned.begin(), owned.end()});
    if (!mapping)
    {
        return mapping;
    }
    static std::atomic<std::uint64_t> sequence{};
    const auto token = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                       std::to_string(sequence.fetch_add(1));
    Staging staging(root / (".meta-stage-" + token));
    const auto created = staging.Create();
    if (!created)
    {
        return created;
    }
    struct Candidate
    {
        Path Staged;
        Path Destination;
    };
    std::vector<Candidate> candidates;
    for (const auto& file : files)
    {
        const auto destination = root / file.RelativePath;
        const bool exists = std::filesystem::exists(destination, error);
        if (error)
        {
            return Failed("无法检查输出文件");
        }
        if (exists)
        {
            const auto content = Read(destination);
            if (!content)
            {
                return std::unexpected(content.error());
            }
            if (*content == file.Content)
            {
                continue;
            }
        }
        const auto staged = staging.Add(file.Content);
        if (!staged)
        {
            return std::unexpected(staged.error());
        }
        candidates.push_back({.Staged = *staged, .Destination = destination});
    }
    const auto unionIndex = staging.Add(Index(owned));
    const auto finalIndex = staging.Add(Index(requested));
    const auto stagedDepfile = staging.Add(*depfile);
    const auto stamp = staging.Add(token + '\n');
    if (!unionIndex)
    {
        return std::unexpected(unionIndex.error());
    }
    if (!finalIndex)
    {
        return std::unexpected(finalIndex.error());
    }
    if (!stagedDepfile)
    {
        return std::unexpected(stagedDepfile.error());
    }
    if (!stamp)
    {
        return std::unexpected(stamp.error());
    }
    // 所有候选均已成功落在同卷 staging；从此处开始旧 stamp 不再代表成功。
    std::filesystem::remove(root / "Meta.stamp", error);
    if (error)
    {
        return Failed("无法撤销旧成功标记");
    }
    std::size_t ordinal = 0;
    const auto replace = [&](const Path& source, const Path& destination) -> ToolResult<void>
    {
        if (options.BeforeReplace)
        {
            const auto before = options.BeforeReplace(destination, ordinal);
            if (!before)
            {
                return before;
            }
        }
        ++ordinal;
        const auto checked = CheckPath(destination, false);
        if (!checked)
        {
            return checked;
        }
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error)
        {
            return Failed("无法创建输出父目录");
        }
        return ReplaceFile(source, destination);
    };
    // 先登记并集，再修改任何代码。中断后重试仍能清理新旧批次的所有拥有文件。
    auto result = replace(*unionIndex, root / "Meta.outputs.json");
    if (!result)
    {
        return result;
    }
    for (const auto& candidate : candidates)
    {
        result = replace(candidate.Staged, candidate.Destination);
        if (!result)
        {
            return result;
        }
    }
    result = replace(*stagedDepfile, root / "Meta.d");
    if (!result)
    {
        return result;
    }
    for (const auto& relative : *old)
    {
        if (requested.contains(relative))
        {
            continue;
        }
        const auto destination = root / relative;
        const auto checked = CheckPath(destination, false);
        if (!checked)
        {
            return checked;
        }
        std::filesystem::remove(destination, error);
        if (error)
        {
            return Failed("删除废弃输出失败: " + Utf8(relative));
        }
    }
    // 删除全部成功后才能缩小 ownership，最后替换 stamp 推进本轮原生构建状态。
    result = replace(*finalIndex, root / "Meta.outputs.json");
    if (!result)
    {
        return result;
    }
    return replace(*stamp, root / "Meta.stamp");
}
ToolResult<void> Publish(const GenerationRequest& request, std::span<const GeneratedFile> files,
                         std::span<const Path> dependencies)
{
    return Publish(request, files, dependencies, {});
}
} // namespace pg::meta::tool
