#pragma once

#include <MetaInfo.h>
#include <ModuleIdentity.h>
#include <span>
#include <typeinfo>

namespace pg::module
{
struct ModuleMetaEntry
{
    const std::type_info* CppType;
    const meta::TypeInfo* Info;
};

// 表与 RTTI 均由所属 Module 存储；接纳后不可修改。
struct ModuleMetaProvider
{
    ModuleId Owner;
    std::span<const ModuleMetaEntry> Entries;
};
} // namespace pg::module
