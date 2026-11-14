#include "ContextParser.h"
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/FileSystemOptions.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/ADT/SmallString.h>
#include <memory>
#include <string>
#include <vector>

namespace pg::meta::tool::test
{
namespace
{
class CapturedDiagnostics final : public clang::DiagnosticConsumer
{
public:
    explicit CapturedDiagnostics(std::vector<std::string>& messages)
        : Messages(&messages)
    {
    }

    void HandleDiagnostic(clang::DiagnosticsEngine::Level level, const clang::Diagnostic& info) override
    {
        clang::DiagnosticConsumer::HandleDiagnostic(level, info);
        llvm::SmallString<256> message;
        info.FormatDiagnostic(message);
        Messages->emplace_back(message.c_str());
    }

private:
    std::vector<std::string>* Messages;
};
} // namespace

bool ParseContext(const std::vector<std::string>& arguments, std::vector<std::string>& messages)
{
    messages.clear();
    CapturedDiagnostics diagnostics(messages);
    llvm::IntrusiveRefCntPtr<clang::FileManager> const files(new clang::FileManager(clang::FileSystemOptions{}));
    clang::tooling::ToolInvocation tool(arguments, std::make_unique<clang::SyntaxOnlyAction>(), files.get());
    tool.setDiagnosticConsumer(&diagnostics);
    return tool.run() && diagnostics.getNumErrors() == 0;
}
} // namespace pg::meta::tool::test
