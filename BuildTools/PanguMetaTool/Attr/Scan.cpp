#include "Attr/Scan.h"
#include "Attr/ConditionGuard.h"
#include "Attr/FieldAttribute.h"
#include "Attr/OptionsReader.h"
#include "Attr/TypeAttribute.h"
#include "Driver/CompilationContext.h"
#include "Driver/Diagnostic.h"
#include "Driver/GenerationRequest.h"
#include "Driver/InputOwnership.h"
#include "Driver/OutputMapping.h"
#include "MetaIR/Model.h"
#include "MetaIR/Normalize.h"
#include "MetaIR/SourceLocation.h"
#include "MetaOptions.h"
#include <algorithm>
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/FileSystemOptions.h>
#include <clang/Basic/Specifiers.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Tooling/Tooling.h>
#include <expected>
#include <filesystem>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace pg::meta::tool
{
namespace
{
using Path = std::filesystem::path;
std::string Utf8(const Path& path)
{
    const auto value = path.generic_u8string();
    return {value.begin(), value.end()};
}
class ScanDiagnostics final : public clang::DiagnosticConsumer
{
public:
    explicit ScanDiagnostics(Diagnostics& errors)
        : Errors(errors)
    {
    }
    void HandleDiagnostic(clang::DiagnosticsEngine::Level level, const clang::Diagnostic& info) override
    {
        clang::DiagnosticConsumer::HandleDiagnostic(level, info);
        if (level < clang::DiagnosticsEngine::Error)
        {
            return;
        }
        llvm::SmallString<256> text;
        info.FormatDiagnostic(text);
        const auto source =
            info.hasSourceManager() ? CopyLocation(info.getSourceManager(), info.getLocation()) : ir::SourceLocation{};
        Errors.push_back({.Code = "invalid_context", .Message = std::string(text), .Source = source});
    }

private:
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    Diagnostics& Errors;
};
const clang::AnnotateAttr* Annotation(const clang::Decl& decl, std::string_view name)
{
    for (const auto* attr : decl.specific_attrs<clang::AnnotateAttr>())
    {
        if (attr->getAnnotation() == llvm::StringRef(name))
        {
            return attr;
        }
    }
    return nullptr;
}
class Collector final : public clang::RecursiveASTVisitor<Collector>
{
public:
    Collector(clang::ASTContext& context, const GenerationRequest& request, const InputOwnership& ownership,
              ir::Model& model, Diagnostics& errors, const ConditionState& conditions)
        : Context(context)
        , Request(request)
        , Ownership(ownership)
        , Model(model)
        , Errors(errors)
        , Conditions(conditions)
    {
    }
    // Clang CRTP visitor 通过同名静态回调分派。
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    bool VisitCXXRecordDecl(clang::CXXRecordDecl* decl)
    {
        if (decl->getQualifiedNameAsString() == "pg::meta::FieldOptions" && decl->isCompleteDefinition())
        {
            const auto source = CopyLocation(Context.getSourceManager(), decl->getLocation());
            std::error_code error;
            if (std::filesystem::equivalent(Path(std::u8string(source.File.begin(), source.File.end())),
                                            Request.EngineSource / "Source/Runtime/Foundation/Public/MetaOptions.h",
                                            error) &&
                !error)
            {
                Options = decl;
            }
        }
        return true;
    }
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    bool VisitDecl(clang::Decl* decl)
    {
        if ((Annotation(*decl, TypeAttribute::kSchema.Annotation) != nullptr) ||
            (Annotation(*decl, FieldAttribute::kSchema.Annotation) != nullptr))
        {
            Declarations.push_back(decl);
        }
        return true;
    }
    void Collect()
    {
        TraverseDecl(Context.getTranslationUnitDecl());
        for (const auto* decl : Declarations)
        {
            const auto source = CopyLocation(Context.getSourceManager(), decl->getLocation());
            if (Path(source.File).extension() == ".cpp" || Path(source.File).extension() == ".cc")
            {
                Error("unsupported_declaration", "反射声明必须来自登记的原始头，不能只在业务 cpp 中定义", *decl);
                continue;
            }
            const auto owner = Ownership.Find(Path(std::u8string(source.File.begin(), source.File.end())));
            if (!owner)
            {
                Errors.insert(Errors.end(), owner.error().begin(), owner.error().end());
                continue;
            }
            if (!*owner)
            {
                Error("unregistered_input", "包含 pga 的项目头未登记归属", *decl);
                continue;
            }
            const auto* type = Annotation(*decl, TypeAttribute::kSchema.Annotation);
            const auto* field = Annotation(*decl, FieldAttribute::kSchema.Annotation);
            unsigned count = 0;
            for (const auto* attr : decl->specific_attrs<clang::AnnotateAttr>())
            {
                if (attr->getAnnotation() == llvm::StringRef(TypeAttribute::kSchema.Annotation) ||
                    attr->getAnnotation() == llvm::StringRef(FieldAttribute::kSchema.Annotation))
                {
                    ++count;
                }
            }
            if (count != 1)
            {
                Error("unsupported_declaration", "同一声明不支持重复或混合 pga 属性", *decl);
                continue;
            }
            const auto* annotation = (type != nullptr) ? type : field;
            if (DependsOnFrontend(decl->getLocation(), Context.getSourceManager(), Conditions) ||
                DependsOnFrontend(annotation->getLocation(), Context.getSourceManager(), Conditions))
            {
                Error("unsupported_condition", "反射声明依赖前端条件包含或宏展开来源", *decl);
            }
            for (auto* argument : annotation->args())
            {
                if (DependsOnFrontend(*argument, Context.getSourceManager(), Conditions))
                {
                    Error("unsupported_condition", "属性参数依赖前端条件声明或其调用依赖无法静态确定", *decl);
                }
            }
            if (type != nullptr)
            {
                auto value = ReadType(*decl, *type);
                if (value)
                {
                    Model.Types.push_back(std::move(*value));
                }
            }
            if (field != nullptr)
            {
                const auto* member = llvm::dyn_cast<clang::FieldDecl>(decl);
                if ((member == nullptr) || member->isBitField() || member->getType()->isReferenceType())
                {
                    Error("unsupported_declaration", "Field 仅支持普通直接非位域、非引用成员", *decl);
                }
                else if (Annotation(*member->getParent(), TypeAttribute::kSchema.Annotation) == nullptr)
                {
                    Error("orphan_field", "Field 的所属完整类型缺少 Type", *decl);
                }
            }
        }
    }

private:
    void Error(std::string code, std::string message, const clang::Decl& decl)
    {
        Errors.push_back({
            .Code = std::move(code),
            .Message = std::move(message),
            .Source = CopyLocation(Context.getSourceManager(), decl.getLocation()),
        });
    }
    static bool Nameable(const clang::CXXRecordDecl& record)
    {
        if (record.isUnion() || (record.getIdentifier() == nullptr) || !record.isCompleteDefinition() ||
            (record.isLocalClass() != nullptr) || record.isDependentType() ||
            (record.getDescribedClassTemplate() != nullptr) ||
            llvm::isa<clang::ClassTemplateSpecializationDecl>(record))
        {
            return false;
        }
        for (const clang::DeclContext* context = &record; context != nullptr; context = context->getParent())
        {
            if (const auto* enclosing = llvm::dyn_cast<clang::CXXRecordDecl>(context);
                enclosing != nullptr &&
                (enclosing->getIdentifier() == nullptr || enclosing->getAccess() == clang::AS_private ||
                 enclosing->getAccess() == clang::AS_protected))
            {
                return false;
            }
            if (const auto* space = llvm::dyn_cast<clang::NamespaceDecl>(context);
                (space != nullptr) && space->isAnonymousNamespace())
            {
                return false;
            }
        }
        return true;
    }
    std::optional<ir::TypeDefinition> ReadType(const clang::Decl& decl, const clang::AnnotateAttr& annotation)
    {
        const auto* record = llvm::dyn_cast<clang::CXXRecordDecl>(&decl);
        if ((record == nullptr) || !Nameable(*record))
        {
            Error("unsupported_declaration", "Type 必须是生成 TU 可合法命名的非模板完整 class/struct", decl);
            return {};
        }
        ir::TypeDefinition type{
            .QualifiedName = record->getQualifiedNameAsString(),
            .Id = {},
            .Source = CopyLocation(Context.getSourceManager(), decl.getLocation()),
            .Fields = {},
        };
        if (annotation.args_size() != 0)
        {
            clang::Expr::EvalResult evaluation;
            const auto* expr = *annotation.args_begin();
            if (!expr->EvaluateAsConstantExpr(evaluation, Context) || evaluation.DiagEmitted)
            {
                Error("invalid_constant", "TypeId 必须为常量字符串", decl);
                return {};
            }
            auto text = ReadConstantString(evaluation.Val, Context);
            if (!text)
            {
                Error("invalid_type_id", "TypeId 必须为非空常量字符串", decl);
                return {};
            }
            const auto id = ir::ParseTypeId(*text);
            if (!id)
            {
                Error("invalid_type_id", id.error().front().Message, decl);
                return {};
            }
            type.Id = *id;
        }
        for (const auto* member : record->fields())
        {
            const auto* attr = Annotation(*member, FieldAttribute::kSchema.Annotation);
            if (attr == nullptr)
            {
                continue;
            }
            if (Options == nullptr)
            {
                Error("invalid_options", "未找到规范 FieldOptions 声明", *member);
                continue;
            }
            const auto options = ReadOptions(
                Context, *Options, (attr->args_size() != 0U) ? *attr->args_begin() : nullptr, member->getName());
            if (!options)
            {
                for (const auto& error : options.error())
                {
                    Error(error.Code, error.Message, *member);
                }
                continue;
            }
            clang::PrintingPolicy policy(Context.getLangOpts());
            policy.SuppressTagKeyword = true;
            policy.FullyQualifiedName = true;
            auto name = member->getType().getAsString(policy);
            if (name.contains("anonymous") || name.contains("unnamed"))
            {
                name = "decltype(" + type.QualifiedName + "::" + member->getNameAsString() + ")";
            }
            auto visibility = Access::kPublic;
            if (member->getAccess() == clang::AS_private)
            {
                visibility = Access::kPrivate;
            }
            else if (member->getAccess() == clang::AS_protected)
            {
                visibility = Access::kProtected;
            }
            type.Fields.push_back({
                .Name = member->getNameAsString(),
                .Key = options->Key,
                .CppType = std::move(name),
                .Source = CopyLocation(Context.getSourceManager(), member->getLocation()),
                .Visibility = visibility,
                .IsConst = member->getType().isConstQualified(),
                .ReadOnly = options->ReadOnly,
            });
        }
        return type;
    }
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    clang::ASTContext& Context;
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const GenerationRequest& Request;
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const InputOwnership& Ownership;
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    ir::Model& Model;
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    Diagnostics& Errors;
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const ConditionState& Conditions;
    const clang::CXXRecordDecl* Options{};
    std::vector<const clang::Decl*> Declarations;
};
class Consumer final : public clang::ASTConsumer
{
public:
    Consumer(const GenerationRequest& request, const InputOwnership& ownership, ScanResult& result, Diagnostics& errors,
             const ConditionState& conditions)
        : Request(request)
        , Ownership(ownership)
        , Result(result)
        , Errors(errors)
        , Conditions(conditions)
    {
    }
    void HandleTranslationUnit(clang::ASTContext& context) override
    {
        Collector(context, Request, Ownership, Result.Model, Errors, Conditions).Collect();
    }

private:
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const GenerationRequest& Request;
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const InputOwnership& Ownership;
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    ScanResult& Result;
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    Diagnostics& Errors;
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const ConditionState& Conditions;
};
class Action final : public clang::ASTFrontendAction
{
public:
    Action(const GenerationRequest& request, const InputOwnership& ownership, ScanResult& result, Diagnostics& errors)
        : Request(request)
        , Ownership(ownership)
        , Result(result)
        , Errors(errors)
    {
    }

protected:
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& compiler,
                                                          llvm::StringRef /*InFile*/) override
    {
        // 原生诊断原文由 ScanDiagnostics 返回，避免另向 stderr 打印无上下文计数。
        compiler.getDiagnosticOpts().ShowCarets = false;
        InstallConditionGuard(compiler.getPreprocessor(), Errors, Result.Dependencies, Conditions);
        return std::make_unique<Consumer>(Request, Ownership, Result, Errors, Conditions);
    }

private:
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const GenerationRequest& Request;
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const InputOwnership& Ownership;
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    ScanResult& Result;
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    Diagnostics& Errors;
    ConditionState Conditions;
};
} // namespace
void RegisterBuiltinAttributes()
{
    static std::once_flag registered;
    std::call_once(registered,
                   []
                   {
                       RegisterTypeAttribute();
                       RegisterFieldAttribute();
                   });
}
ToolResult<ScanResult> Scan(const GenerationRequest& request)
{
    const auto mapping = ValidateOutputMapping(request);
    if (!mapping)
    {
        return std::unexpected(mapping.error());
    }
    const auto ownership = InputOwnership::Create(request.Ownership);
    if (!ownership)
    {
        return std::unexpected(ownership.error());
    }
    RegisterBuiltinAttributes();
    ScanResult result;
    result.Model.ContextId = request.Target;
    Diagnostics errors;
    for (const auto& header : request.Headers)
    {
        auto invocation = BuildCompilationInvocation(request, header);
        if (!invocation)
        {
            return std::unexpected(invocation.error());
        }
        result.Dependencies.insert(result.Dependencies.end(), invocation->Dependencies.begin(),
                                   invocation->Dependencies.end());
        invocation->Arguments.insert(invocation->Arguments.begin(),
                                     {
                                         PANGU_META_HOST_COMPILER_PATH,
                                         "-Xclang",
                                         "-include",
                                         "-Xclang",
                                         Utf8(request.EngineSource / "Source/Runtime/Foundation/Public/MetaOptions.h"),
                                     });
        ScanDiagnostics diagnostics(errors);
        const auto files = llvm::makeIntrusiveRefCnt<clang::FileManager>(clang::FileSystemOptions{});
        clang::tooling::ToolInvocation tool(invocation->Arguments,
                                            std::make_unique<Action>(request, *ownership, result, errors), files.get());
        tool.setDiagnosticConsumer(&diagnostics);
        const bool success = tool.run();
        if (!success && errors.empty())
        {
            errors.push_back({.Code = "invalid_context", .Message = "Clang 前端任务失败"});
        }
    }
    if (!errors.empty())
    {
        return std::unexpected(std::move(errors));
    }
    // ID 的唯一性域是 Module；foreign 仍独立规范化，不能因不发射而跳过校验。
    std::map<std::string, ir::Model> domains;
    for (auto& type : result.Model.Types)
    {
        const auto owner = ownership->Find(Path(std::u8string(type.Source.File.begin(), type.Source.File.end())));
        if (!owner)
        {
            return std::unexpected(owner.error());
        }
        if (!*owner)
        {
            return std::unexpected(Diagnostics{
                {.Code = "unregistered_input", .Message = "反射声明缺少 Module 归属", .Source = type.Source},
            });
        }
        const auto [entry, inserted] = domains.try_emplace(std::string(**owner));
        (void)inserted;
        entry->second.ContextId = entry->first;
        entry->second.Types.push_back(std::move(type));
    }
    result.Model.Types.clear();
    for (auto& [owner, input] : domains)
    {
        auto model = ir::Normalize(std::move(input));
        if (!model)
        {
            return std::unexpected(model.error());
        }
        if (owner == request.Target)
        {
            result.Model = std::move(*model);
        }
    }
    // 在真实文件系统边界完成唯一绑定，保持登记路径的原始拼写及映射槽位。
    // Dependencies 仍用 canonical 路径；纯 Generate 可直接组合此 Model，无须重复文件探测。
    for (auto& type : result.Model.Types)
    {
        const Path original(std::u8string(type.Source.File.begin(), type.Source.File.end()));
        const HeaderInput* bound = nullptr;
        for (const auto& header : request.Headers)
        {
            std::error_code error;
            const bool same = std::filesystem::equivalent(original, header.Path, error);
            if (error)
            {
                return std::unexpected(Diagnostics{
                    {
                        .Code = "unregistered_input",
                        .Message = "无法核对类型与登记头的文件身份",
                        .Source = type.Source,
                    },
                });
            }
            if (!same)
            {
                continue;
            }
            if (bound != nullptr)
            {
                return std::unexpected(Diagnostics{
                    {.Code = "duplicate_owner", .Message = "类型对应多个登记头别名", .Source = type.Source},
                });
            }
            bound = &header;
        }
        if (bound == nullptr)
        {
            return std::unexpected(Diagnostics{
                {.Code = "unregistered_input", .Message = "当前 Module 类型缺少登记头", .Source = type.Source},
            });
        }
        type.Source.File = Utf8(bound->Path);
    }
    std::ranges::sort(result.Dependencies);
    result.Dependencies.erase(std::ranges::unique(result.Dependencies).begin(), result.Dependencies.end());
    return result;
}
} // namespace pg::meta::tool
