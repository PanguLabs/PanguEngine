#pragma once
#include "Attr/AttributeSchema.h"
namespace pg::meta::tool
{
struct TypeAttribute
{
    static constexpr AttributeSchema kSchema{
        .Spelling = "pga::Type",
        .Name = "Type",
        .Annotation = "pangu.type",
        .Required = 0,
        .Optional = 1,
    };
};
void RegisterTypeAttribute();
} // namespace pg::meta::tool
