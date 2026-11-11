#pragma once

#include <cstdint>
#include <string>

namespace pg::meta::ir
{
struct SourceLocation
{
    std::string File;
    std::uint32_t Line{};
    std::uint32_t Column{};
};
} // namespace pg::meta::ir
