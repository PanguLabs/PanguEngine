#include "Driver/Diagnostic.h"
#include "Driver/GenerationRequest.h"
#include "Driver/OutputMapping.h"
#include "Output/PublishInternal.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <ranges>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <array>
#include <string_view>
#define NOMINMAX
// Windows SDK 的分片头依赖 Windows.h 的类型和顺序。
#include <Windows.h> // IWYU pragma: keep
#include <fileapi.h>
#include <handleapi.h>
#include <minwinbase.h>
#include <minwindef.h>
#include <winbase.h>
#include <winnt.h>
#elifdef __linux__
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace pg::meta::tool
{
namespace
{
using Path = std::filesystem::path;
Path IoPath(Path path)
{
#ifdef _WIN32
    path.make_preferred();
    const auto& text = path.native();
    if (text.starts_with(L"\\\\?\\"))
    {
        return path;
    }
    if (text.starts_with(L"\\\\"))
    {
        return {L"\\\\?\\UNC\\" + text.substr(2)};
    }
    return {L"\\\\?\\" + text};
#else
    return path;
#endif
}
std::unexpected<Diagnostics> Collision(std::string message, const Path& path = {})
{
    const auto text = path.generic_u8string();
    return std::unexpected(Diagnostics{
        {
            .Code = "output_collision",
            .Message = std::move(message),
            .Source = {.File = std::string(text.begin(), text.end())},
        },
    });
}
bool Inside(const Path& file, const Path& root)
{
    const auto relative = file.lexically_relative(root);
    return !relative.empty() && !relative.has_root_path() &&
           std::ranges::none_of(relative, [](const auto& part) { return part == ".."; });
}
ToolResult<Path> Canonical(const Path& path)
{
    std::error_code error;
    auto canonical = std::filesystem::weakly_canonical(path, error);
    if (error)
    {
        return Collision("无法确定真实路径: " + error.message(), path);
    }
    return canonical;
}
ToolResult<bool> Equivalent(const Path& left, const Path& right)
{
    std::error_code error;
    // libc++ 对两条都缺失的路径可报告非 ENOENT；先区分正常缺失与身份比较失败。
    for (const auto& path : {left, right})
    {
        const bool exists = std::filesystem::exists(path, error);
        if (error)
        {
            return Collision("无法检查文件存在性", path);
        }
        if (!exists)
        {
            return false;
        }
    }
    const auto same = std::filesystem::equivalent(left, right, error);
    if (error && error != std::errc::no_such_file_or_directory)
    {
        return Collision("无法比较真实文件身份", left);
    }
    return same;
}

ToolResult<void> ValidateDirectoryMode(const Path& directory)
{
#ifdef _WIN32
    // 默认 NTFS 的命名规则由同目录内的临时 namespace 实测，不自行实现 Unicode 大小写表。
    auto* const handle = CreateFileW(IoPath(directory).c_str(), FILE_READ_ATTRIBUTES,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                     FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return Collision("无法读取生成目录能力", directory);
    }
    FILE_CASE_SENSITIVE_INFO sensitivity{};
    std::array<wchar_t, 32> filesystem{};
    const bool supported =
        (GetFileInformationByHandleEx(handle, FileCaseSensitiveInfo, &sensitivity, sizeof(sensitivity)) != 0) &&
        sensitivity.Flags == 0 &&
        (GetVolumeInformationByHandleW(handle, nullptr, 0, nullptr, nullptr, nullptr, filesystem.data(),
                                       static_cast<DWORD>(filesystem.size())) != 0) &&
        std::wstring_view(filesystem.data()) == L"NTFS";
    CloseHandle(handle);
    if (!supported)
    {
        return Collision("当前仅支持默认不区分大小写的 NTFS 生成目录", directory);
    }
#elifdef __linux__
    // ext4 可以逐目录启用 casefold；未复制此能力到探测树时必须拒绝特殊目录。
    // 原生 open 的可变参数仅在 O_CREAT 时使用，此处无需第三参数。
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const int descriptor = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor < 0)
    {
        return Collision("无法读取生成目录能力", directory);
    }
    unsigned long flags = 0;
    // FS_IOC_GETFLAGS 的 ABI 要求 unsigned long 指针。
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const bool supported = ioctl(descriptor, FS_IOC_GETFLAGS, &flags) == 0 && (flags & FS_CASEFOLD_FL) == 0;
    close(descriptor);
    if (!supported)
    {
        return Collision("生成目录须支持能力查询且未启用逐目录 casefold", directory);
    }
#else
    // APFS 的卷级名称规则由同卷探测判断，不把宿主 locale 当文件身份。
    (void)directory;
#endif
    return {};
}

class MappingProbe
{
public:
    explicit MappingProbe(Path root)
        : Root(std::move(root))
    {
    }
    MappingProbe(const MappingProbe&) = delete;
    MappingProbe& operator=(const MappingProbe&) = delete;
    MappingProbe(MappingProbe&& other) noexcept
        : Root(std::exchange(other.Root, {}))
        , Created(std::move(other.Created))
    {
    }
    MappingProbe& operator=(MappingProbe&&) = delete;
    ~MappingProbe()
    {
        // 正常路径显式返回 Clear 的错误；析构只负责异常早退时的最后一次清理尝试。
        const auto cleanup = Clear();
        (void)cleanup;
    }
    [[nodiscard]] static ToolResult<MappingProbe> Create(const Path& root)
    {
        static std::atomic<std::uint64_t> sequence{};
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 16; ++attempt)
        {
            const auto path = IoPath(
                root / (".meta-path-probe-" + std::to_string(tick) + "-" + std::to_string(sequence.fetch_add(1))));
            std::error_code error;
            if (std::filesystem::create_directory(path, error))
            {
                return MappingProbe(path);
            }
            if (error)
            {
                return Collision("无法创建文件身份探测目录: " + std::to_string(error.value()), path);
            }
        }
        return Collision("无法独占文件身份探测目录", root);
    }
    [[nodiscard]] ToolResult<void> Add(const Path& relative)
    {
        const auto text = relative.generic_u8string();
        if (text.contains(u8':') || text.contains(u8'\0'))
        {
            return Collision("输出不得使用平台设备或数据流路径", relative);
        }
        const auto file = IoPath(Root / relative);
        std::error_code error;
        auto directory = Root;
        for (const auto& part : relative.parent_path())
        {
            directory /= part;
            if (std::filesystem::create_directory(directory, error))
            {
                Created.push_back(directory);
            }
            if (error)
            {
                return Collision("输出父目录与其他输出冲突", relative);
            }
        }
        // 独占创建不截断前一槽位：Unicode、大小写和文件/目录别名均由实际 FS 决定。
        const auto written = WriteExclusive(file, {});
        if (!written)
        {
            return Collision("输出槽位在真实文件系统中冲突或不可创建", relative);
        }
        Created.push_back(file);
        if (!std::filesystem::is_regular_file(file, error) || error)
        {
            return Collision("输出不是普通文件路径", relative);
        }
        return {};
    }
    [[nodiscard]] ToolResult<void> Clear()
    {
        if (Root.empty())
        {
            return {};
        }
        std::error_code error;
        // 只逆序移除本次创建的占位与目录，不递归删除后来出现的外来文件。
        for (const auto& path : std::views::reverse(Created))
        {
            std::filesystem::remove(path, error);
            if (error)
            {
                return Collision("清理文件身份占位失败", path);
            }
        }
        Created.clear();
        std::filesystem::remove(Root, error);
        if (error)
        {
            return Collision("清理文件身份探测目录失败", Root);
        }
        Root.clear();
        return {};
    }

private:
    Path Root;
    std::vector<Path> Created;
};

ToolResult<std::vector<Path>> ValidateExisting(const GenerationRequest& request, const Path& root,
                                               const std::vector<Path>& mapping)
{
    for (const auto& header : request.Headers)
    {
        const auto path = Canonical(header.Path);
        const auto sourceRoot = Canonical(header.Root);
        if (!path)
        {
            return std::unexpected(path.error());
        }
        if (!sourceRoot)
        {
            return std::unexpected(sourceRoot.error());
        }
        if (!Inside(*path, *sourceRoot))
        {
            return Collision("输入真实路径逃逸所属根", header.Path);
        }
    }
    std::vector<Path> paths;
    for (const auto& relative : mapping)
    {
        const auto absolute = Canonical(request.OutputRoot / relative);
        if (!absolute)
        {
            return std::unexpected(absolute.error());
        }
        if (!Inside(*absolute, root))
        {
            return Collision("输出真实路径逃逸生成根", relative);
        }
        std::error_code error;
        if (std::filesystem::is_directory(*absolute, error))
        {
            return Collision("输出槽位已被目录占用", relative);
        }
        for (auto parent = absolute->parent_path(); Inside(parent, root); parent = parent.parent_path())
        {
            error.clear();
            if (std::filesystem::is_directory(parent, error))
            {
                const auto mode = ValidateDirectoryMode(parent);
                if (!mode)
                {
                    return std::unexpected(mode.error());
                }
            }
        }
        for (const auto& previous : paths)
        {
            const auto equivalent = Equivalent(*absolute, previous);
            if (!equivalent)
            {
                return std::unexpected(equivalent.error());
            }
            if (*equivalent || *absolute == previous)
            {
                return Collision("输出真实文件身份冲突", relative);
            }
        }
        for (const auto* state : {"Meta.outputs.json", "Meta.d", "Meta.stamp"})
        {
            const auto equivalent = Equivalent(*absolute, root / state);
            if (!equivalent)
            {
                return std::unexpected(equivalent.error());
            }
            if (*equivalent)
            {
                return Collision("输出与批次状态文件冲突", relative);
            }
        }
        for (const auto* reserved : {
                 "Private/ModuleMeta.gen.h",
                 "Private/ModuleMeta.gen.cpp",
                 "Private/ModuleQuery.gen.cpp",
                 "Private/StaticModuleCatalog.gen.cpp",
             })
        {
            if (std::ranges::find(mapping, Path(reserved)) != mapping.end())
            {
                // 已启用槽位由本批输出之间的比较覆盖，不能把自身视为冲突。
                continue;
            }
            const auto equivalent = Equivalent(*absolute, root / reserved);
            if (!equivalent)
            {
                return std::unexpected(equivalent.error());
            }
            if (*equivalent)
            {
                return Collision("输出与永久保留汇总槽位的真实文件身份冲突", relative);
            }
        }
        paths.push_back(*absolute);
    }
    std::vector<Path> relativePaths;
    relativePaths.reserve(paths.size());
    for (const auto& path : paths)
    {
        relativePaths.push_back(path.lexically_relative(root));
    }
    return relativePaths;
}
} // namespace

ToolResult<void> ValidateOutputMapping(const GenerationRequest& request)
{
    const auto mapping = BuildOutputMapping(request);
    if (!mapping)
    {
        return std::unexpected(mapping.error());
    }
    const auto root = Canonical(request.OutputRoot);
    const auto build = Canonical(request.Owner == SourceOwner::kEngine ? request.EngineBuild : request.ProjectBuild);
    if (!root)
    {
        return std::unexpected(root.error());
    }
    if (!build)
    {
        return std::unexpected(build.error());
    }
    if (!request.OutputRoot.is_absolute() || !Inside(*root, *build) || *root == *build)
    {
        return Collision("生成根不在 owned BuildRoot 内", request.OutputRoot);
    }
    const auto existing = ValidateExisting(request, *root, *mapping);
    if (!existing)
    {
        return std::unexpected(existing.error());
    }
    std::error_code error;
    std::filesystem::create_directories(*root, error);
    if (error)
    {
        return Collision("无法访问生成根", *root);
    }
    const auto mode = ValidateDirectoryMode(*root);
    if (!mode)
    {
        return mode;
    }
    auto probe = MappingProbe::Create(*root);
    if (!probe)
    {
        return std::unexpected(probe.error());
    }
    ToolResult<void> status;
    for (const auto* reserved : {
             "Private/ModuleMeta.gen.h",
             "Private/ModuleMeta.gen.cpp",
             "Private/ModuleQuery.gen.cpp",
             "Private/StaticModuleCatalog.gen.cpp",
         })
    {
        if (std::ranges::find(*mapping, Path(reserved)) == mapping->end())
        {
            const auto destination = Canonical(*root / Path(reserved));
            if (!destination)
            {
                status = std::unexpected(destination.error());
                break;
            }
            if (!Inside(*destination, *root))
            {
                status = Collision("保留汇总槽位逃逸生成根", Path(reserved));
                break;
            }
            status = probe->Add(destination->lexically_relative(*root));
            if (!status)
            {
                break;
            }
        }
    }
    for (const auto& relative : *existing)
    {
        if (!status)
        {
            break;
        }
        status = probe->Add(relative);
        if (!status)
        {
            break;
        }
    }
    const auto cleanup = probe->Clear();
    if (!cleanup)
    {
        return cleanup;
    }
    return status;
}
ToolResult<void> ValidatePublishPaths(const std::filesystem::path& root,
                                      const std::vector<std::filesystem::path>& paths)
{
    GenerationRequest request;
    request.OutputRoot = root;
    const auto existing = ValidateExisting(request, root, paths);
    if (!existing)
    {
        return std::unexpected(existing.error());
    }
    const auto mode = ValidateDirectoryMode(root);
    if (!mode)
    {
        return mode;
    }
    auto probe = MappingProbe::Create(root);
    if (!probe)
    {
        return std::unexpected(probe.error());
    }
    ToolResult<void> status;
    for (const auto* state : {"Meta.outputs.json", "Meta.d", "Meta.stamp"})
    {
        status = probe->Add(state);
        if (!status)
        {
            break;
        }
    }
    for (const auto& relative : *existing)
    {
        if (!status)
        {
            break;
        }
        status = probe->Add(relative);
    }
    const auto cleanup = probe->Clear();
    return cleanup ? status : cleanup;
}
} // namespace pg::meta::tool
