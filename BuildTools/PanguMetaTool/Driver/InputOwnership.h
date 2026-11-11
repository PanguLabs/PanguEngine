#pragma once
#include "Driver/GenerationRequest.h"
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>
namespace pg::meta::tool
{
class InputOwnership
{
public:
    [[nodiscard]] static ToolResult<InputOwnership> Create(std::span<const OwnershipEntry> entries);
    // 返回视图仅借用本归属表；调用者必须保持表存活。
    [[nodiscard]] ToolResult<std::optional<std::string_view>> Find(const std::filesystem::path& path) const;

private:
    explicit InputOwnership(std::vector<OwnershipEntry> entries)
        : Entries(std::move(entries))
    {
    }
    std::vector<OwnershipEntry> Entries;
};

} // namespace pg::meta::tool
