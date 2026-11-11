#pragma once

#include <MetaIdentity.h>
#include <MetaOptions.h>

#include <optional>
#include <span>
#include <string_view>

namespace pg::meta
{
struct FieldInfo
{
    std::string_view Name;
    std::string_view Key;
    std::string_view TypeName;
    Access Visibility;
    bool IsConst;
    bool IsReadOnly;
};

// 视图借用目标产物存储，不拥有字符串或字段数组。
struct TypeInfo
{
    std::string_view Name;
    std::optional<TypeId> Id;
    std::span<const FieldInfo> Fields;
};
} // namespace pg::meta
