#pragma once
#include <filesystem>
#include <string>
namespace pg::meta::tool
{
struct GeneratedFile
{
    std::filesystem::path RelativePath;
    std::string Content;
};
} // namespace pg::meta::tool
