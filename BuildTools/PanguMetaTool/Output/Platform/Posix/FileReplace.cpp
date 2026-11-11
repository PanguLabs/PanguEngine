#include "Driver/Diagnostic.h"
#include "Output/PublishInternal.h"
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <unistd.h>

namespace pg::meta::tool
{
ToolResult<void> WriteExclusive(const std::filesystem::path& path, std::string_view content)
{
    // Apple 系统 libc++ 可能尚不实现 fstream::noreplace，使用原生 O_EXCL 保持契约。
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (descriptor < 0)
    {
        const int failure = errno;
        return std::unexpected(
            Diagnostics{{.Code = "publish_failed", .Message = "创建 staging 候选失败: " + std::to_string(failure)}});
    }
    int failure = 0;
    while (!content.empty())
    {
        const auto bytes = write(descriptor, content.data(), std::min(content.size(), std::size_t{1048576}));
        if (bytes < 0 && errno == EINTR)
        {
            continue;
        }
        if (bytes <= 0)
        {
            failure = bytes < 0 ? errno : EIO;
            break;
        }
        content.remove_prefix(static_cast<std::size_t>(bytes));
    }
    if (close(descriptor) != 0 && failure == 0)
    {
        failure = errno;
    }
    if (failure != 0)
    {
        // 文件由本次 O_EXCL 创建；仅删除该单文件，失败时由下一轮独立 namespace 重试。
        unlink(path.c_str());
        return std::unexpected(
            Diagnostics{{.Code = "publish_failed", .Message = "写入 staging 候选失败: " + std::to_string(failure)}});
    }
    return {};
}
bool IsPlatformOutputPath(const std::filesystem::path& /*relative*/) { return true; }
ToolResult<void> ReplaceFile(const std::filesystem::path& staged, const std::filesystem::path& destination)
{
    if (std::rename(staged.c_str(), destination.c_str()) != 0)
    {
        return std::unexpected(
            Diagnostics{{.Code = "publish_failed", .Message = "原子替换失败: " + std::to_string(errno)}});
    }
    return {};
}
} // namespace pg::meta::tool
