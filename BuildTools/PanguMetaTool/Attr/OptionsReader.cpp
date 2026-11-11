#include "Attr/OptionsReader.h"
#include "Driver/Diagnostic.h"
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/OperationKinds.h>
#include <clang/Basic/Specifiers.h>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <llvm/Support/Casting.h>
#include <llvm/Support/ConvertUTF.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace pg::meta::tool
{
namespace
{
std::unexpected<Diagnostics> Invalid(std::string code, std::string message)
{
    return std::unexpected(Diagnostics{{.Code = std::move(code), .Message = std::move(message)}});
}
} // namespace
ToolResult<std::string> ReadConstantString(const clang::APValue& value, const clang::ASTContext& context)
{
    if (!value.isLValue() || value.isNullPointer())
    {
        return Invalid("invalid_constant", "需要非空字符串常量");
    }
    const auto base = value.getLValueBase();
    std::string text;
    if (const auto* expr = base.dyn_cast<const clang::Expr*>())
    {
        const auto* literal = llvm::dyn_cast<clang::StringLiteral>(expr->IgnoreParenImpCasts());
        if ((literal == nullptr) || !literal->isOrdinary())
        {
            return Invalid("invalid_constant", "需要 UTF-8 char 字符串");
        }
        text = literal->getBytes().str();
        text.push_back('\0');
    }
    else if (const auto* decl = llvm::dyn_cast_if_present<clang::VarDecl>(base.dyn_cast<const clang::ValueDecl*>()))
    {
        const auto* array = context.getAsConstantArrayType(decl->getType());
        if (array == nullptr || !array->getElementType()->isCharType())
        {
            return Invalid("invalid_constant", "字符串必须指向常量字符数组");
        }
        // 地址是常量并不保证内容可读：构造真实数组的逐元素 lvalue-to-rvalue AST，
        // 交给 Clang 的语言常量求值器判断，而不是复制可 fold 的初始化器。
        // DeclRefExpr API 要求非 const 声明指针；这里只建立引用，不修改声明。
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        auto* reference = clang::DeclRefExpr::Create(context, {}, {}, const_cast<clang::VarDecl*>(decl), false,
                                                     decl->getLocation(), decl->getType(), clang::VK_LValue);
        auto* pointer =
            clang::ImplicitCastExpr::Create(context, context.getPointerType(array->getElementType()),
                                            clang::CK_ArrayToPointerDecay, reference, nullptr, clang::VK_PRValue, {});
        const auto sizeType = context.getSizeType();
        for (std::uint64_t i = 0; i < array->getSize().getZExtValue(); ++i)
        {
            auto* index = clang::IntegerLiteral::Create(context, llvm::APInt(context.getIntWidth(sizeType), i),
                                                        sizeType, decl->getLocation());
            // ASTContext 的 arena 拥有这些验证节点，与本次 AST 一起销毁。
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
            auto* element = new (context) clang::ArraySubscriptExpr(
                pointer, index, array->getElementType(), clang::VK_LValue, clang::OK_Ordinary, decl->getLocation());
            // 此 placement new 重载由 AST arena 统一回收，分析器不能识别其宿主所有权。
            // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
            const auto* read =
                clang::ImplicitCastExpr::Create(context, array->getElementType().getUnqualifiedType(),
                                                clang::CK_LValueToRValue, element, nullptr, clang::VK_PRValue, {});
            clang::Expr::EvalResult evaluation;
            if (!read->EvaluateAsConstantExpr(evaluation, context) || evaluation.DiagEmitted ||
                evaluation.HasSideEffects || evaluation.HasUndefinedBehavior || !evaluation.Val.isInt())
            {
                return Invalid("invalid_constant", "所指字符内容不满足 C++ 常量表达式读取规则");
            }
            text.push_back(static_cast<char>(evaluation.Val.getInt().getZExtValue()));
        }
    }
    else
    {
        {
            return Invalid("invalid_constant", "字符串必须具有稳定常量存储");
        }
    }
    const auto offset = value.getLValueOffset().getQuantity();
    if (offset < 0 || static_cast<std::uint64_t>(offset) >= text.size())
    {
        return Invalid("invalid_constant", "字符串偏移越界");
    }
    text.erase(0, static_cast<std::size_t>(offset));
    if (text.empty() || text.back() != '\0')
    {
        return Invalid("invalid_constant", "字符串缺少结束符");
    }
    text.pop_back();
    if (text.empty() || text.contains('\0'))
    {
        return Invalid("invalid_constant", "字符串不得为空或含内嵌零");
    }
    const std::vector<llvm::UTF8> bytes(text.begin(), text.end());
    const auto* cursor = bytes.data();
    // LLVM UTF-8 接口使用起止指针，end 仅取已拥有容器的尾后位置。
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (llvm::isLegalUTF8String(&cursor, cursor + bytes.size()) == 0U)
    {
        return Invalid("invalid_constant", "字符串不是合法 UTF-8");
    }
    return text;
}
ToolResult<FieldOptionsValue> ReadOptions(const clang::ASTContext& context, const clang::CXXRecordDecl& canonical,
                                          const clang::Expr* expression, std::string_view fieldName)
{
    clang::APValue options;
    if (expression != nullptr)
    {
        if (expression->isTypeDependent() ||
            expression->getType()->getAsCXXRecordDecl() != canonical.getCanonicalDecl())
        {
            return Invalid("invalid_options", "Field 参数必须是规范 pg::meta::FieldOptions 类型");
        }
        clang::Expr::EvalResult evaluation;
        if (!expression->EvaluateAsConstantExpr(evaluation, context) || evaluation.DiagEmitted ||
            evaluation.HasSideEffects || evaluation.HasUndefinedBehavior || !evaluation.Val.isStruct())
        {
            return Invalid("invalid_constant", "FieldOptions 必须为编译期常量");
        }
        options = std::move(evaluation.Val);
    }
    FieldOptionsValue result{.Key = std::string(fieldName)};
    unsigned index = 0;
    for (const auto* field : canonical.fields())
    {
        clang::APValue value;
        if (expression != nullptr)
        {
            {
                value = options.getStructField(index);
            }
        }
        else
        {
            clang::Expr::EvalResult evaluation;
            const auto* init = field->getInClassInitializer();
            if ((init == nullptr) || !init->EvaluateAsRValue(evaluation, context))
            {
                return Invalid("invalid_options", "规范 Options 缺少常量默认初始化器");
            }
            value = std::move(evaluation.Val);
        }
        if (field->getName() == "Key")
        {
            if (!value.isLValue() || !value.isNullPointer())
            {
                auto key = ReadConstantString(value, context);
                if (!key)
                {
                    return std::unexpected(key.error());
                }
                result.Key = std::move(*key);
            }
        }
        else if (field->getName() == "ReadOnly" && value.isInt())
        {
            {
                result.ReadOnly = value.getInt().getBoolValue();
            }
        }
        else
        {
            {
                return Invalid("invalid_options", "规范 FieldOptions schema 不匹配");
            }
        }
        ++index;
    }
    return result;
}
} // namespace pg::meta::tool
