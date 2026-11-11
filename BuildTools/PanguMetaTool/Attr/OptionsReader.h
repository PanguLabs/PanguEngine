#pragma once
#include "Driver/Diagnostic.h"
#include <string>
namespace clang
{
class ASTContext;
class CXXRecordDecl;
class Expr;
class APValue;
} // namespace clang
namespace pg::meta::tool
{
struct FieldOptionsValue
{
    std::string Key;
    bool ReadOnly{};
};
[[nodiscard]] ToolResult<std::string> ReadConstantString(const clang::APValue& value, const clang::ASTContext& context);
[[nodiscard]] ToolResult<FieldOptionsValue> ReadOptions(const clang::ASTContext& context,
                                                        const clang::CXXRecordDecl& canonical,
                                                        const clang::Expr* expression, std::string_view fieldName);
} // namespace pg::meta::tool
