#pragma once

#include "MetaIR/SourceLocation.h"
#include <MetaIdentity.h>
#include <MetaOptions.h>
#include <optional>
#include <string>
#include <vector>

namespace pg::meta::ir
{
struct FieldDefinition
{
    std::string Name;
    std::string Key;
    std::string CppType;
    SourceLocation Source{};
    Access Visibility{Access::kPublic};
    bool IsConst{};
    bool ReadOnly{};
};

struct TypeDefinition
{
    std::string QualifiedName;
    std::optional<TypeId> Id;
    SourceLocation Source{};
    std::vector<FieldDefinition> Fields;
};

struct Model
{
    std::string ContextId;
    std::vector<TypeDefinition> Types;
};
} // namespace pg::meta::ir
