#pragma once

#include <filesystem>
#include <string>

namespace pg::build::plugin
{
// 文件系统保留原生路径，JSON 与诊断边界统一使用 UTF-8。
[[nodiscard]] inline std::string PathToUtf8(const std::filesystem::path& path, bool generic = false)
{
    const auto encoded = generic ? path.generic_u8string() : path.u8string();
    return {encoded.begin(), encoded.end()};
}
} // namespace pg::build::plugin
