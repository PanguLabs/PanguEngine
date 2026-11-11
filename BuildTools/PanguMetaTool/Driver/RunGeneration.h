#pragma once
#include "Driver/GenerationRequest.h"
namespace pg::meta::tool
{
[[nodiscard]] ToolResult<void> RunGeneration(const GenerationRequest& request);
namespace detail
{
// CLI 溯源不进入 v1 JSON 或类型 IR；直接调用者可不提供请求文件。
[[nodiscard]] ToolResult<void> RunGenerationFromFile(const GenerationRequest& request,
                                                     const std::filesystem::path& source);
} // namespace detail
} // namespace pg::meta::tool
