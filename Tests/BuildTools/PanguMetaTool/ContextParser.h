#pragma once

#include <string>
#include <vector>

namespace pg::meta::tool::test
{
[[nodiscard]] bool ParseContext(const std::vector<std::string>& arguments, std::vector<std::string>& messages);
}
