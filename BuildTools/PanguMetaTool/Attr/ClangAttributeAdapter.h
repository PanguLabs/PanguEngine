#pragma once
#include <array>
#include <clang/AST/Attr.h>
#include <clang/Basic/ParsedAttrInfo.h>
#include <clang/Sema/Initialization.h>
#include <clang/Sema/Lookup.h>
#include <clang/Sema/ParsedAttr.h>
#include <clang/Sema/Sema.h>
#include <string_view>
namespace pg::meta::tool
{
// 注册器只持有不可变 schema；单次参数归 AST 声明所有。
template <class Schema>
struct ClangAttributeAdapter final : clang::ParsedAttrInfo
{
    ClangAttributeAdapter()
    {
        NumArgs = Schema::kSchema.Required;
        OptArgs = Schema::kSchema.Optional;
        Spellings = kSpellings;
    }
    [[nodiscard]] bool isParamExpr(std::size_t /*N*/) const override { return true; }
    AttrHandling handleDeclAttribute(clang::Sema& sema, clang::Decl* decl, const clang::ParsedAttr& attr) const override
    {
        if ((attr.getScopeName() == nullptr) || attr.getScopeName()->getName() != "pga" ||
            attr.getAttrName()->getName() != Schema::kSchema.Name)
        {
            sema.Diag(attr.getLoc(),
                      sema.getDiagnostics().getCustomDiagID(clang::DiagnosticsEngine::Error,
                                                            "unsupported_declaration: pga 属性必须使用精确命名空间"));
            return AttributeNotApplied;
        }
        llvm::SmallVector<clang::Expr*, 1> args;
        for (unsigned i = 0; i < attr.getNumArgs(); ++i)
        {
            if (attr.isArgExpr(i))
            {
                {
                    args.push_back(attr.getArgAsExpr(i));
                }
            }
            else
            {
                const auto* identifier = attr.getArgAsIdent(i);
                clang::LookupResult lookup(sema, identifier->getIdentifierInfo(), identifier->getLoc(),
                                           clang::Sema::LookupOrdinaryName);
                sema.LookupName(lookup, sema.getCurScope());
                clang::CXXScopeSpec const scope;
                const auto expression = sema.BuildDeclarationNameExpr(scope, lookup, false);
                if (expression.isInvalid())
                {
                    return AttributeNotApplied;
                }
                args.push_back(expression.get());
            }
        }
        for (auto*& expression : args)
        {
            const auto converted = sema.DefaultFunctionArrayLvalueConversion(expression);
            if (converted.isInvalid())
            {
                return AttributeNotApplied;
            }
            expression = converted.get();
            if (!expression->isTypeDependent() && expression->getType()->isRecordType())
            {
                const auto copied = sema.PerformCopyInitialization(
                    clang::InitializedEntity::InitializeTemporary(expression->getType()), attr.getLoc(), expression);
                if (copied.isInvalid())
                {
                    return AttributeNotApplied;
                }
                expression = copied.get();
            }
        }
        decl->addAttr(clang::AnnotateAttr::Create(sema.Context, Schema::kSchema.Annotation, args.data(),
                                                  static_cast<unsigned>(args.size()), attr.getRange()));
        return AttributeApplied;
    }

private:
    // SDK 的 hasAttribute 插件查询使用裸名；语义回调仍严格要求 pga scope。
    static constexpr std::array kSpellings{
        Spelling{clang::AttributeCommonInfo::AS_CXX11, Schema::kSchema.Spelling},
        Spelling{clang::AttributeCommonInfo::AS_CXX11, Schema::kSchema.Name},
    };
};
} // namespace pg::meta::tool
