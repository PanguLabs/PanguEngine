#pragma once
#include "Attr/AttributeSchema.h"
namespace pg::meta::tool
{
struct FieldAttribute
{
    static constexpr AttributeSchema kSchema{
        .Spelling = "pga::Field",
        .Name = "Field",
        .Annotation = "pangu.field",
        .Required = 0,
        .Optional = 1,
    };
};
void RegisterFieldAttribute();
} // namespace pg::meta::tool
