#pragma once
#include <string_view>
namespace pg::meta::tool
{
struct AttributeSchema
{
    const char* Spelling;
    const char* Name;
    std::string_view Annotation;
    unsigned Required;
    unsigned Optional;
};
} // namespace pg::meta::tool
