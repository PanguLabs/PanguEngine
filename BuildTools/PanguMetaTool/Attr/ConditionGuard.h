#pragma once
#include "Driver/Diagnostic.h"
#include <clang/Basic/SourceLocation.h>
#include <filesystem>
#include <vector>
namespace clang
{
class Preprocessor;
class SourceManager;
class Expr;
} // namespace clang
namespace pg::meta::tool
{
[[nodiscard]] ir::SourceLocation CopyLocation(const clang::SourceManager& sources, clang::SourceLocation location);
struct ConditionState
{
    std::vector<clang::SourceRange> Ranges;
};
[[nodiscard]] bool DependsOnFrontend(clang::SourceLocation location, const clang::SourceManager& sources,
                                     const ConditionState& state);
[[nodiscard]] bool DependsOnFrontend(clang::Expr& expression, const clang::SourceManager& sources,
                                     const ConditionState& state);
void InstallConditionGuard(clang::Preprocessor& preprocessor, Diagnostics& errors,
                           std::vector<std::filesystem::path>& dependencies, ConditionState& state);
} // namespace pg::meta::tool
