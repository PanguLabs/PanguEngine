#pragma once
#include "Driver/GenerationRequest.h"
#include "Output/GeneratedFile.h"
#include <span>
namespace pg::meta::tool
{
// 调用者保证同一 Target/Config/OutputRoot 串行，传入已成功生成的完整批次。
// 单文件替换具有原子性；失败后须重试，Meta.stamp 仅在全部成功后发布。
[[nodiscard]] ToolResult<void> Publish(const GenerationRequest& request, std::span<const GeneratedFile> files,
                                       std::span<const std::filesystem::path> dependencies);
} // namespace pg::meta::tool
