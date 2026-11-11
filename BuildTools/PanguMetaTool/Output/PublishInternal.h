#pragma once
#include "Output/Publish.h"
#include <functional>
#include <string_view>
namespace pg::meta::tool
{
// 仅供工具实现与正式失败恢复测试使用，不进入 CLI。
struct PublishOptions
{
    // 每次实际替换前调用：绝对目标路径、从 0 开始的替换序号。
    // 顺序为恢复 index、内容变化的代码、depfile、最终 index、stamp；相同代码跳过。
    std::function<ToolResult<void>(const std::filesystem::path&, std::size_t)> BeforeReplace;
};
[[nodiscard]] ToolResult<void> Publish(const GenerationRequest& request, std::span<const GeneratedFile> files,
                                       std::span<const std::filesystem::path> dependencies,
                                       const PublishOptions& options);
[[nodiscard]] ToolResult<void> ReplaceFile(const std::filesystem::path& staged,
                                           const std::filesystem::path& destination);
[[nodiscard]] bool IsPlatformOutputPath(const std::filesystem::path& relative);
// 创建失败不触碰已有文件；创建后写入失败只清理本次独占创建的文件。
[[nodiscard]] ToolResult<void> WriteExclusive(const std::filesystem::path& path, std::string_view content);
[[nodiscard]] ToolResult<void> ValidatePublishPaths(const std::filesystem::path& root,
                                                    const std::vector<std::filesystem::path>& paths);
} // namespace pg::meta::tool
