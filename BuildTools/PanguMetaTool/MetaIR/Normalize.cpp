#include "MetaIR/Normalize.h"
#include "Driver/Diagnostic.h"
#include "MetaIR/Model.h"
#include "MetaIR/SourceLocation.h"
#include "MetaIdentity.h"
#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <map>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace pg::meta::ir
{
namespace
{
int HexDigit(char value)
{
    if (value >= '0' && value <= '9')
    {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f')
    {
        return value - 'a' + 10;
    }
    return -1;
}

bool SameSource(const SourceLocation& left, const SourceLocation& right)
{
    if (left.Line != right.Line || left.Column != right.Column)
    {
        return false;
    }
    const std::filesystem::path leftPath{std::u8string(left.File.begin(), left.File.end())};
    const std::filesystem::path rightPath{std::u8string(right.File.begin(), right.File.end())};
    if (leftPath == rightPath)
    {
        return true;
    }
    std::error_code error;
    return std::filesystem::equivalent(leftPath, rightPath, error) && !error;
}

bool SameDefinition(const TypeDefinition& left, const TypeDefinition& right)
{
    return left.Id == right.Id && SameSource(left.Source, right.Source) &&
           std::ranges::equal(left.Fields, right.Fields,
                              [](const auto& a, const auto& b)
                              {
                                  return a.Name == b.Name && a.Key == b.Key && a.CppType == b.CppType &&
                                         SameSource(a.Source, b.Source) && a.Visibility == b.Visibility &&
                                         a.IsConst == b.IsConst && a.ReadOnly == b.ReadOnly;
                              });
}
} // namespace

tool::ToolResult<TypeId> ParseTypeId(std::string_view text)
{
    TypeId id;
    bool valid = text.size() == id.Bytes.size() * 2;
    if (valid)
    {
        for (auto& byte : id.Bytes)
        {
            const int high = HexDigit(text.front());
            text.remove_prefix(1);
            const int low = HexDigit(text.front());
            text.remove_prefix(1);
            if (high < 0 || low < 0)
            {
                valid = false;
                break;
            }
            byte = static_cast<std::byte>((high * 16) + low);
        }
    }
    if (!valid || !id.IsValid())
    {
        return std::unexpected(
            tool::Diagnostics{{.Code = "invalid_type_id", .Message = "TypeId 必须是非全零的 32 位小写十六进制字符串"}});
    }
    return id;
}

tool::ToolResult<Model> Normalize(Model model)
{
    for (auto& type : model.Types)
    {
        if (type.QualifiedName.starts_with("::"))
        {
            type.QualifiedName.erase(0, 2);
        }
        if (type.QualifiedName.empty() || type.Source.File.empty())
        {
            return std::unexpected(tool::Diagnostics{
                {
                    .Code = "unsupported_declaration",
                    .Message = "类型缺少规范限定名或声明源位置",
                    .Source = type.Source,
                },
            });
        }
        if (type.Id && !type.Id->IsValid())
        {
            return std::unexpected(tool::Diagnostics{
                {.Code = "invalid_type_id", .Message = "显式 TypeId 不得全零", .Source = type.Source},
            });
        }
        std::set<std::string> keys;
        for (auto& field : type.Fields)
        {
            if (field.Key.empty())
            {
                field.Key = field.Name;
            }
            if (field.Name.empty() || field.CppType.empty() || field.Key.empty())
            {
                return std::unexpected(tool::Diagnostics{
                    {.Code = "unsupported_declaration", .Message = "字段缺少名称或 C++ 类型", .Source = field.Source},
                });
            }
            if (!keys.insert(field.Key).second)
            {
                return std::unexpected(tool::Diagnostics{
                    {
                        .Code = "duplicate_field_key",
                        .Message = "同类型字段 Key 重复: " + field.Key,
                        .Source = field.Source,
                    },
                });
            }
        }
    }
    std::ranges::sort(model.Types, {}, &TypeDefinition::QualifiedName);
    std::vector<TypeDefinition> types;
    std::map<TypeId, std::string> identities;
    for (auto& type : model.Types)
    {
        if (!types.empty() && types.back().QualifiedName == type.QualifiedName)
        {
            if (!SameDefinition(types.back(), type))
            {
                return std::unexpected(tool::Diagnostics{
                    {
                        .Code = "unsupported_declaration",
                        .Message = "同名类型的 canonical 声明或定义冲突: " + type.QualifiedName,
                        .Source = type.Source,
                    },
                });
            }
            continue;
        }
        if (type.Id && !identities.emplace(*type.Id, type.QualifiedName).second)
        {
            return std::unexpected(tool::Diagnostics{
                {.Code = "duplicate_type_id", .Message = "不同类型使用相同的显式 TypeId", .Source = type.Source},
            });
        }
        types.push_back(std::move(type));
    }
    model.Types = std::move(types);
    return model;
}
} // namespace pg::meta::ir
