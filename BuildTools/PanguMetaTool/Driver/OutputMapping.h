#pragma once
#include "Driver/GenerationRequest.h"
namespace pg::meta::tool
{
// 纯槽位计算与声明集合校验，不依赖 Clang 或平台 API。
[[nodiscard]] ToolResult<std::vector<std::filesystem::path>> BuildOutputMapping(const GenerationRequest& request);
// Scan/Driver 在使用映射前检查实际文件与尚不存在文件的别名。
[[nodiscard]] ToolResult<void> ValidateOutputMapping(const GenerationRequest& request);
} // namespace pg::meta::tool
