#pragma once
#include <string>
#include <string_view>
namespace pg::meta::tool
{
[[nodiscard]] std::string QuoteCppString(std::string_view value);
[[nodiscard]] std::string EscapeIdentifier(std::string_view value);
[[nodiscard]] std::string GlobalName(std::string_view value);
} // namespace pg::meta::tool
