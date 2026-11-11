#pragma once

#include "MetaIR/SourceLocation.h"
#include <expected>
#include <string>
#include <vector>

namespace pg::meta::tool
{
struct Diagnostic
{
    std::string Code;
    std::string Message;
    ir::SourceLocation Source{};
};
using Diagnostics = std::vector<Diagnostic>;

template <class T>
using ToolResult = std::expected<T, Diagnostics>;
} // namespace pg::meta::tool
