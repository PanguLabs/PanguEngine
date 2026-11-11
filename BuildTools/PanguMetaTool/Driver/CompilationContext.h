#pragma once
#include "Driver/GenerationRequest.h"
namespace pg::meta::tool
{
struct CompilationInvocation
{
    std::vector<std::string> Arguments;
    std::vector<std::filesystem::path> Dependencies;
};
[[nodiscard]] ToolResult<CompilationInvocation> BuildCompilationInvocation(const GenerationRequest& request,
                                                                           const HeaderInput& header);
// 返回不含程序名的 Driver 参数；原始头位于末项，消费者使用配套宿主编译器作为 argv[0]。
[[nodiscard]] ToolResult<std::vector<std::string>> BuildInvocation(const GenerationRequest& request,
                                                                   const HeaderInput& header);
} // namespace pg::meta::tool
