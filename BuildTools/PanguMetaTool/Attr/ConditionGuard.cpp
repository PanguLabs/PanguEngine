#include "Attr/ConditionGuard.h"
#include "Driver/Diagnostic.h"
#include "MetaIR/SourceLocation.h"
#include <algorithm>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticIDs.h>
#include <clang/Basic/FileEntry.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <cstddef>
#include <filesystem>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>
#include <memory>
#include <set>
#include <system_error>
#include <vector>
namespace pg::meta::tool
{
ir::SourceLocation CopyLocation(const clang::SourceManager& sources, clang::SourceLocation location)
{
    if (location.isInvalid())
    {
        return {};
    }
    const auto expansion = sources.getExpansionLoc(location);
    const auto presumed = sources.getPresumedLoc(expansion);
    if (presumed.isInvalid())
    {
        return {};
    }
    const auto entry = sources.getFileEntryRefForID(sources.getFileID(expansion));
    const auto name = entry ? entry->getName() : llvm::StringRef(presumed.getFilename());
    std::error_code error;
    const auto canonical =
        std::filesystem::canonical(std::filesystem::path(std::u8string(name.begin(), name.end())), error);
    const auto utf8 = canonical.generic_u8string();
    return {
        .File = error ? name.str() : std::string(utf8.begin(), utf8.end()),
        .Line = presumed.getLine(),
        .Column = presumed.getColumn(),
    };
}
bool DependsOnFrontend(clang::SourceLocation location, const clang::SourceManager& sources, const ConditionState& state)
{
    std::vector<clang::SourceLocation> pending{location};
    std::set<unsigned> visited;
    while (!pending.empty())
    {
        const auto current = pending.back();
        pending.pop_back();
        if (current.isInvalid() || !visited.insert(current.getRawEncoding()).second)
        {
            continue;
        }
        if (current.isMacroID())
        {
            pending.push_back(sources.getImmediateSpellingLoc(current));
            pending.push_back(sources.getImmediateExpansionRange(current).getBegin());
            continue;
        }
        if (std::ranges::any_of(state.Ranges, [&](const auto range)
                                { return sources.isPointWithin(current, range.getBegin(), range.getEnd()); }))
        {
            return true;
        }
        pending.push_back(sources.getIncludeLoc(sources.getFileID(current)));
    }
    return false;
}
namespace
{
class ConditionGuard final : public clang::PPCallbacks
{
public:
    ConditionGuard(clang::Preprocessor& pp, Diagnostics& errors, std::vector<std::filesystem::path>& dependencies,
                   ConditionState& state)
        : Pp(pp)
        , Errors(errors)
        , Dependencies(dependencies)
        , State(state)
    {
    }
    void FileChanged(clang::SourceLocation location, FileChangeReason reason,
                     clang::SrcMgr::CharacteristicKind /*FileType*/, clang::FileID /*PrevFID*/) override
    {
        if (reason != EnterFile)
        {
            return;
        }
        const auto& sources = Pp.getSourceManager();
        const auto id = sources.getFileID(location);
        const auto entry = sources.getFileEntryRefForID(id);
        if (!entry)
        {
            return;
        }
        const auto source = CopyLocation(sources, location);
        Dependencies.emplace_back(std::u8string(source.File.begin(), source.File.end()));
        if (!sources.isInSystemHeader(location))
        {
            Files.insert(id);
        }
    }
    void InclusionDirective(clang::SourceLocation hashLocation, const clang::Token& /*includeToken*/,
                            llvm::StringRef /*fileName*/, bool /*isAngled*/, clang::CharSourceRange /*filenameRange*/,
                            clang::OptionalFileEntryRef /*file*/, llvm::StringRef /*searchPath*/,
                            llvm::StringRef /*relativePath*/, const clang::Module* /*suggestedModule*/,
                            bool /*moduleImported*/, clang::SrcMgr::CharacteristicKind /*fileType*/) override
    {
        ObservedIncludes.insert(hashLocation.getRawEncoding());
    }
    void If(clang::SourceLocation location, clang::SourceRange range, ConditionValueKind /*ConditionValue*/) override
    {
        Check(location, range);
    }
    void Elif(clang::SourceLocation /*Loc*/, clang::SourceRange range, ConditionValueKind /*ConditionValue*/,
              clang::SourceLocation ifLocation) override
    {
        Check(ifLocation, range);
    }
    void Ifdef(clang::SourceLocation location, const clang::Token& token, const clang::MacroDefinition& /*MD*/) override
    {
        Check(location, {token.getLocation(), token.getLocation()});
    }
    void Ifndef(clang::SourceLocation location, const clang::Token& token,
                const clang::MacroDefinition& /*MD*/) override
    {
        Check(location, {token.getLocation(), token.getLocation()});
    }
    void Elifdef(clang::SourceLocation location, const clang::Token& token,
                 const clang::MacroDefinition& /*MD*/) override
    {
        Check(location, {token.getLocation(), token.getLocation()});
    }
    void Elifdef(clang::SourceLocation /*location*/, clang::SourceRange range,
                 clang::SourceLocation ifLocation) override
    {
        Check(ifLocation, range);
    }
    void Elifndef(clang::SourceLocation location, const clang::Token& token,
                  const clang::MacroDefinition& /*MD*/) override
    {
        Check(location, {token.getLocation(), token.getLocation()});
    }
    void Elifndef(clang::SourceLocation /*location*/, clang::SourceRange range,
                  clang::SourceLocation ifLocation) override
    {
        Check(ifLocation, range);
    }
    void MacroExpands(const clang::Token& token, const clang::MacroDefinition& definition, clang::SourceRange range,
                      const clang::MacroArgs* /*Args*/) override
    {
        const auto& sources = Pp.getSourceManager();
        ObservedMacros.insert(sources.getExpansionLoc(token.getLocation()).getRawEncoding());
        if (sources.isInSystemHeader(token.getLocation()))
        {
            return;
        }
        const auto name = token.getIdentifierInfo()->getName();
        bool unsafe = name.starts_with("__clang") || name.starts_with("__GNUC") || name == "_MSC_VER";
        if (name == "__has_cpp_attribute")
        {
            // 此 builtin 的回调 range 可能只含宏名，参数仍从原始文件缓冲区读取。
            const auto location = sources.getExpansionLoc(token.getLocation());
            const auto remaining =
                sources.getBufferData(sources.getFileID(location)).drop_front(sources.getFileOffset(location));
            const auto end = remaining.find(')');
            const auto text = end == llvm::StringRef::npos ? remaining : remaining.take_front(end + 1);
            unsafe = text.contains("pga") || text.contains("Type") || text.contains("Field");
        }
        const auto* macro = definition.getMacroInfo();
        if (macro != nullptr)
        {
            for (const auto condition : State.Ranges)
            {
                if (sources.isPointWithin(macro->getDefinitionLoc(), condition.getBegin(), condition.getEnd()))
                {
                    unsafe = true;
                }
            }
        }
        if (unsafe)
        {
            State.Ranges.emplace_back(sources.getExpansionLoc(range.getBegin()),
                                      sources.getExpansionLoc(range.getEnd()));
        }
    }
    void Endif(clang::SourceLocation location, clang::SourceLocation ifLocation) override
    {
        const auto inCondition = [&](unsigned candidate)
        {
            return Pp.getSourceManager().isPointWithin(clang::SourceLocation::getFromRawEncoding(candidate), ifLocation,
                                                       location);
        };
        if (!std::ranges::any_of(Unsafe, inCondition))
        {
            return;
        }
        std::erase_if(Unsafe, inCondition);
        State.Ranges.emplace_back(ifLocation, location);
        const auto text = clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(ifLocation, location),
                                                      Pp.getSourceManager(), Pp.getLangOpts());
        // 包括未激活分支中的实际属性 token，忽略注释、字符串及无关的属性探测。
        clang::Lexer lexer(ifLocation, Pp.getLangOpts(), text.begin(), text.begin(), text.end());
        clang::Token token;
        bool bracket = false;
        bool inAttribute = false;
        bool reflection = false;
        clang::SourceLocation directive;
        while (!lexer.LexFromRawLexer(token))
        {
            if (token.is(clang::tok::hash))
            {
                directive = token.getLocation();
                continue;
            }
            if (token.is(clang::tok::raw_identifier))
            {
                const auto name = token.getRawIdentifier();
                if (directive.isValid() && (name == "include" || name == "include_next" || name == "import") &&
                    !ObservedIncludes.contains(directive.getRawEncoding()))
                {
                    Errors.push_back({
                        .Code = "unsupported_condition",
                        .Message = "不支持未选择的前端条件 include；无法从实际预处理路径验证该输入",
                        .Source = CopyLocation(Pp.getSourceManager(), directive),
                    });
                }
                if (MayReflect(Pp.getIdentifierInfo(name),
                               !ObservedMacros.contains(token.getLocation().getRawEncoding())))
                {
                    reflection = true;
                }
            }
            directive = {};
            if (token.is(clang::tok::l_square))
            {
                inAttribute = inAttribute || bracket;
                bracket = true;
                continue;
            }
            bracket = false;
            if (token.is(clang::tok::r_square))
            {
                inAttribute = false;
            }
            if (inAttribute && token.is(clang::tok::raw_identifier) && token.getRawIdentifier() == "pga")
            {
                reflection = true;
            }
        }
        if (reflection)
        {
            Errors.push_back({
                .Code = "unsupported_condition",
                .Message = "反射声明或参数不得依赖 pga 属性探测或前端版本条件",
                .Source = CopyLocation(Pp.getSourceManager(), ifLocation),
            });
        }
    }
    void EndOfMainFile() override
    {
        // 检查实际 pragma 映射，不禁止系统头合法使用 diagnostic pragma。
        const auto& sources = Pp.getSourceManager();
        for (const auto id : Files)
        {
            const auto buffer = sources.getBufferData(id);
            const auto start = sources.getLocForStartOfFile(id);
            clang::Lexer lexer(start, Pp.getLangOpts(), buffer.begin(), buffer.begin(), buffer.end());
            clang::Token token;
            while (!lexer.LexFromRawLexer(token))
            {
                if (token.isNot(clang::tok::raw_identifier) || token.getRawIdentifier() != "pga")
                {
                    continue;
                }
                if (Pp.getDiagnostics().getDiagnosticLevel(clang::diag::warn_unknown_attribute_ignored,
                                                           token.getLocation()) == clang::DiagnosticsEngine::Ignored)
                {
                    Errors.push_back({
                        .Code = "unsupported_condition",
                        .Message = "项目 pragma 屏蔽了 pga 属性校验",
                        .Source = CopyLocation(sources, token.getLocation()),
                    });
                }
            }
        }
    }

private:
    // 宏图以当前展开路径截断环；保留原 Token，不改写待解析源码。
    // NOLINTNEXTLINE(misc-no-recursion)
    void ExpandMacroTokens(const clang::MacroInfo& macro, std::vector<clang::Token>& tokens,
                           std::set<const clang::MacroInfo*>& active,
                           std::set<const clang::IdentifierInfo*>& unresolved) const
    {
        if (!active.insert(&macro).second)
        {
            return;
        }
        unresolved.insert(macro.param_begin(), macro.param_end());
        for (const auto& token : macro.tokens())
        {
            const auto* nested = token.is(clang::tok::identifier)
                                     ? Pp.getMacroDefinition(token.getIdentifierInfo()).getMacroInfo()
                                     : nullptr;
            if (nested != nullptr && !nested->tokens_empty())
            {
                ExpandMacroTokens(*nested, tokens, active, unresolved);
            }
            else
            {
                tokens.push_back(token);
            }
        }
        active.erase(&macro);
    }
    bool MayReflect(clang::IdentifierInfo* identifier, bool skipped) const
    {
        const auto* macro = Pp.getMacroDefinition(identifier).getMacroInfo();
        if (macro == nullptr)
        {
            return false;
        }
        std::vector<clang::Token> tokens;
        std::set<const clang::MacroInfo*> active;
        std::set<const clang::IdentifierInfo*> unresolved;
        ExpandMacroTokens(*macro, tokens, active, unresolved);
        // token-paste 需要真实实参替换；跳过分支不猜测它是否会拼出反射宏名。
        if (skipped && std::ranges::any_of(tokens, [](const auto& token) { return token.is(clang::tok::hashhash); }))
        {
            return true;
        }
        bool bracket = false;
        bool inAttribute = false;
        for (const auto& token : tokens)
        {
            if (token.is(clang::tok::l_square))
            {
                inAttribute = inAttribute || bracket;
                bracket = true;
                continue;
            }
            bracket = false;
            if (token.is(clang::tok::r_square))
            {
                inAttribute = false;
            }
            // 未实际展开的参数化属性宏不能证明不产生 pga；选中分支由实际 AST 来源检查。
            if (inAttribute && token.is(clang::tok::identifier) &&
                (token.getIdentifierInfo()->getName() == "pga" ||
                 (skipped && unresolved.contains(token.getIdentifierInfo()))))
            {
                return true;
            }
        }
        return false;
    }
    // 宏依赖图以 visited 截断环，递归仅沿实际定义的有限标识符集合。
    // NOLINTNEXTLINE(misc-no-recursion)
    bool FrontendMacro(clang::IdentifierInfo* identifier, std::set<const clang::IdentifierInfo*>& visited) const
    {
        if ((identifier == nullptr) || !visited.insert(identifier).second)
        {
            return false;
        }
        const auto name = identifier->getName();
        if (name.starts_with("__clang") || name.starts_with("__GNUC") || name == "_MSC_VER")
        {
            return true;
        }
        const auto* macro = Pp.getMacroDefinition(identifier).getMacroInfo();
        if (macro == nullptr)
        {
            return false;
        }
        for (const auto& token : macro->tokens())
        {
            if (token.is(clang::tok::identifier) && FrontendMacro(token.getIdentifierInfo(), visited))
            {
                return true;
            }
        }
        return false;
    }
    void Check(clang::SourceLocation location, clang::SourceRange range)
    {
        if (Pp.getSourceManager().isInSystemHeader(location))
        {
            return;
        }
        for (const auto expansion : State.Ranges)
        {
            if (Pp.getSourceManager().isPointWithin(expansion.getBegin(), range.getBegin(), range.getEnd()))
            {
                Unsafe.insert(location.getRawEncoding());
            }
        }
        const auto text = clang::Lexer::getSourceText(clang::CharSourceRange::getTokenRange(range),
                                                      Pp.getSourceManager(), Pp.getLangOpts());
        std::size_t offset = 0;
        while (offset < text.size())
        {
            const auto first = text.find_first_of("_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", offset);
            if (first == llvm::StringRef::npos)
            {
                break;
            }
            auto last =
                text.find_first_not_of("_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", first);
            if (last == llvm::StringRef::npos)
            {
                last = text.size();
            }
            std::set<const clang::IdentifierInfo*> visited;
            if (FrontendMacro(Pp.getIdentifierInfo(text.slice(first, last)), visited))
            {
                Unsafe.insert(location.getRawEncoding());
            }
            offset = last;
        }
        if (text.contains("__clang") || text.contains("__GNUC") || text.contains("_MSC_VER") ||
            (text.contains("__has_cpp_attribute") &&
             (text.contains("pga") || text.contains("Type") || text.contains("Field"))))
        {
            Unsafe.insert(location.getRawEncoding());
        }
    }
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    clang::Preprocessor& Pp;
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    Diagnostics& Errors;
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    std::vector<std::filesystem::path>& Dependencies;
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    ConditionState& State;
    std::set<clang::FileID> Files;
    std::set<unsigned> Unsafe;
    std::set<unsigned> ObservedIncludes;
    std::set<unsigned> ObservedMacros;
};
class ConditionReferences final : public clang::RecursiveASTVisitor<ConditionReferences>
{
public:
    // 必须遍历聚合的语义初始化形式，包含省略成员的 default-init/default-arg。
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method,readability-identifier-naming)
    [[nodiscard]] static bool shouldVisitImplicitCode() { return true; }
    ConditionReferences(const clang::SourceManager& sources, const ConditionState& state)
        : Sources(sources)
        , State(state)
    {
    }
    bool Check(clang::Expr& expression)
    {
        Pending.push_back(&expression);
        while (!Pending.empty())
        {
            auto* statement = Pending.back();
            Pending.pop_back();
            if (!TraverseStmt(statement))
            {
                return false;
            }
        }
        return true;
    }
    // Clang CRTP 回调收集引用初始化器；Visited 截断引用环。
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    bool VisitExpr(clang::Expr* expression)
    {
        if (Within(expression->getExprLoc()))
        {
            return false;
        }
        if (const auto* call = llvm::dyn_cast<clang::CallExpr>(expression);
            call != nullptr && !TrackFunction(call->getDirectCallee()))
        {
            return false;
        }
        if (const auto* construct = llvm::dyn_cast<clang::CXXConstructExpr>(expression);
            construct != nullptr && !TrackFunction(construct->getConstructor()))
        {
            return false;
        }
        if (const auto* member = llvm::dyn_cast<clang::MemberExpr>(expression);
            member != nullptr && Within(member->getMemberDecl()->getLocation()))
        {
            return false;
        }
        if (auto* reference = llvm::dyn_cast<clang::DeclRefExpr>(expression))
        {
            auto* decl = reference->getDecl();
            if (Within(decl->getLocation()))
            {
                return false;
            }
            if (!Visited.insert(decl).second)
            {
                return true;
            }
            if (auto* variable = llvm::dyn_cast<clang::VarDecl>(decl); (variable != nullptr) && variable->hasInit())
            {
                Pending.push_back(variable->getInit());
            }
        }
        return true;
    }

private:
    bool TrackFunction(const clang::FunctionDecl* function)
    {
        // 间接/虚调用没有此处可证明的唯一实现，不按常量折叠结果猜调用依赖。
        if (function == nullptr)
        {
            return false;
        }
        if (function->getBuiltinID() != 0)
        {
            return true;
        }
        if (const auto* method = llvm::dyn_cast<clang::CXXMethodDecl>(function);
            method != nullptr && method->isVirtual())
        {
            return false;
        }
        if (Within(function->getLocation()))
        {
            return false;
        }
        const auto* definition = function->getDefinition();
        if (definition == nullptr)
        {
            return function->isDefaulted() && function->isTrivial();
        }
        if (Within(definition->getLocation()))
        {
            return false;
        }
        if (!Functions.insert(definition).second)
        {
            return true;
        }
        if (definition->hasBody())
        {
            Pending.push_back(definition->getBody());
        }
        else if (!definition->isDefaulted() || !definition->isTrivial())
        {
            return false;
        }
        if (const auto* constructor = llvm::dyn_cast<clang::CXXConstructorDecl>(definition))
        {
            for (const auto* initializer : constructor->inits())
            {
                Pending.push_back(initializer->getInit());
            }
        }
        return true;
    }
    [[nodiscard]] bool Within(clang::SourceLocation location) const
    {
        return DependsOnFrontend(location, Sources, State);
    }
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const clang::SourceManager& Sources;
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const ConditionState& State;
    std::set<const clang::Decl*> Visited;
    std::set<const clang::FunctionDecl*> Functions;
    std::vector<clang::Stmt*> Pending;
};
} // namespace
bool DependsOnFrontend(clang::Expr& expression, const clang::SourceManager& sources, const ConditionState& state)
{
    return !ConditionReferences(sources, state).Check(expression);
}
void InstallConditionGuard(clang::Preprocessor& preprocessor, Diagnostics& errors,
                           std::vector<std::filesystem::path>& dependencies, ConditionState& state)
{
    // 展开 token 的位置保留实际 pragma 状态，防止宏定义位于 push/pop 外而逃逸检查。
    preprocessor.setTokenWatcher(
        [&preprocessor, &errors](const clang::Token& token)
        {
            if (!token.is(clang::tok::identifier) || token.getIdentifierInfo()->getName() != "pga")
            {
                return;
            }
            const auto& sources = preprocessor.getSourceManager();
            const auto location = sources.getExpansionLoc(token.getLocation());
            if (!sources.isInSystemHeader(location) &&
                preprocessor.getDiagnostics().getDiagnosticLevel(clang::diag::warn_unknown_attribute_ignored,
                                                                 location) == clang::DiagnosticsEngine::Ignored)
            {
                errors.push_back({
                    .Code = "unsupported_condition",
                    .Message = "项目 pragma 屏蔽了 pga 属性校验",
                    .Source = CopyLocation(sources, location),
                });
            }
        });
    preprocessor.addPPCallbacks(std::make_unique<ConditionGuard>(preprocessor, errors, dependencies, state));
}
} // namespace pg::meta::tool
