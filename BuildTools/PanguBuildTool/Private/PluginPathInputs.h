#pragma once

#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace pg::build::plugin
{
// 缓存只在一次发现中复用路径元数据，不保留跨 Configure 的索引。
class PluginPathInputs
{
public:
    PluginPathInputs(const std::filesystem::path& root, const std::filesystem::path& canonicalRoot);
    [[nodiscard]] std::expected<std::vector<std::filesystem::path>, std::string>
    Collect(std::span<const std::filesystem::path> files);

private:
    [[nodiscard]] std::expected<std::optional<std::filesystem::path>, std::string>
    LinkTarget(const std::filesystem::path& path);
    [[nodiscard]] std::expected<void, std::string> Track(const std::filesystem::path& file,
                                                         std::set<std::filesystem::path>& inputs);

    std::filesystem::path Root;
    std::map<std::filesystem::path, std::optional<std::filesystem::path>> Links;
};
} // namespace pg::build::plugin
