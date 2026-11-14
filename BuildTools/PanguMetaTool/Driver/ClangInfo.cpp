#include "Driver/ClangInfo.h"
#include "Attr/FieldAttribute.h"
#include "Attr/TypeAttribute.h"
#include "Driver/Diagnostic.h"
#include <clang/AST/APValue.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/Basic/Version.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/Tooling.h>
#include <expected>
#include <filesystem>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace pg::meta::tool
{
namespace
{
class CheckConsumer final : public clang::ASTConsumer
{
public:
    explicit CheckConsumer(bool& valid)
        : Valid(&valid)
    {
    }

    void HandleTranslationUnit(clang::ASTContext& context) override
    {
        for (const auto* decl : context.getTranslationUnitDecl()->decls())
        {
            const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(decl);
            if (record == nullptr || record->getName() != "CheckValue" || !record->isCompleteDefinition())
            {
                continue;
            }
            const auto* type = record->getAttr<clang::AnnotateAttr>();
            if (type == nullptr || type->getAnnotation() != llvm::StringRef(TypeAttribute::kSchema.Annotation) ||
                type->args_size() != 1)
            {
                continue;
            }
            clang::Expr::EvalResult identity;
            if (!(*type->args_begin())->EvaluateAsConstantExpr(identity, context) || identity.DiagEmitted)
            {
                continue;
            }
            for (const auto* field : record->fields())
            {
                const auto* attr = field->getAttr<clang::AnnotateAttr>();
                if (attr == nullptr || attr->getAnnotation() != llvm::StringRef(FieldAttribute::kSchema.Annotation) ||
                    attr->args_size() != 1)
                {
                    continue;
                }
                clang::Expr::EvalResult options;
                if ((*attr->args_begin())->EvaluateAsConstantExpr(options, context) && !options.DiagEmitted &&
                    options.Val.isStruct() && options.Val.getStructNumFields() == 1 &&
                    options.Val.getStructField(0).isInt() && options.Val.getStructField(0).getInt() == 1)
                {
                    *Valid = true;
                }
            }
        }
    }

private:
    // 仅在同步解析期间借用，结果必须在 AST 销毁前复制出来。
    bool* Valid;
};

class CheckAction final : public clang::ASTFrontendAction
{
public:
    explicit CheckAction(bool& valid)
        : Valid(&valid)
    {
    }

protected:
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& /*compiler*/,
                                                          llvm::StringRef /*file*/) override
    {
        return std::make_unique<CheckConsumer>(*Valid);
    }

private:
    bool* Valid;
};
} // namespace

ToolResult<ClangInfo> CheckClang()
{
    const std::filesystem::path resourceDir{PANGU_META_RESOURCE_PATH};
    std::error_code error;
    if (!std::filesystem::is_regular_file(resourceDir / "include" / "stddef.h", error) || error)
    {
        return std::unexpected(Diagnostics{
            {
                .Code = "toolchain_error",
                .Message = "Clang resource include/stddef.h is missing",
            },
        });
    }
    RegisterTypeAttribute();
    RegisterFieldAttribute();
    // 目标解析保留 RTTI；宿主适配代码是否开启 RTTI 由所链接 Clang 库决定。
    const std::vector<std::string> args{
        "-std=c++23", "-fsyntax-only", "-fno-exceptions", "-frtti", "-Werror", "-resource-dir", resourceDir.string(),
    };
    constexpr auto kSource = R"cpp(
#include <stddef.h>
struct CheckOptions { bool ReadOnly; };
constexpr CheckOptions MakeOptions() { return {.ReadOnly = true}; }
struct [[pga::Type("clang-check")]] CheckValue {
    [[pga::Field(MakeOptions())]] int Number;
};
static_assert(sizeof(CheckValue) >= sizeof(int));
)cpp";
    bool valid = false;
    if (!clang::tooling::runToolOnCodeWithArgs(std::make_unique<CheckAction>(valid), kSource, args, "ClangCheck.cpp",
                                               PANGU_META_HOST_COMPILER_PATH) ||
        !valid)
    {
        return std::unexpected(Diagnostics{
            {
                .Code = "toolchain_error",
                .Message = "Clang libraries could not parse pga attributes and evaluate constant options",
            },
        });
    }
#ifdef _WIN32
    constexpr auto kHostStdlib = "msvc";
#elifdef __linux__
    constexpr auto kHostStdlib = "libstdc++";
#else
    constexpr auto kHostStdlib = "libc++";
#endif
    return ClangInfo{
        .FrontendVersion = CLANG_VERSION_STRING,
        .ResourceDir = resourceDir,
        .HasRtti = PANGU_META_CLANG_HAS_RTTI != 0,
        .HostStdlib = kHostStdlib,
    };
}
} // namespace pg::meta::tool
