#include "CodeGen/QueryEmitter.h"
#include "CodeGen/CppNames.h"
#include "Driver/GenerationRequest.h"
#include "Output/GeneratedFile.h"
#include <set>
#include <string>
#include <utility>
namespace pg::meta::tool
{
GeneratedFile EmitQuery(const GenerationRequest& request)
{
    std::set<std::string> headers{request.Module.QueryHeader};
    if (!request.Module.ExportMacro.empty())
    {
        headers.insert(request.Target + "Export.h");
    }
    std::string content;
    for (const auto& header : headers)
    {
        content += "#include " + QuoteCppString(header) + "\n";
    }
    const auto exportPrefix = request.Module.ExportMacro.empty() ? "" : request.Module.ExportMacro + " ";
    content += "#include <ModuleDescriptor.h>\n\nextern \"C\" " + exportPrefix +
               "const pg::module::ModuleDefinition* PanguModuleQueryV1() noexcept\n{\n"
               "    // Definition 由业务 Module 持有，包装只借用其地址。\n    return " +
               GlobalName(request.Module.QuerySymbol) + "();\n}\n";
    return {.RelativePath = "Private/ModuleQuery.gen.cpp", .Content = std::move(content)};
}
} // namespace pg::meta::tool
