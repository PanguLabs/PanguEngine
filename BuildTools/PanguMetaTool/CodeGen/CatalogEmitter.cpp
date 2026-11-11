#include "CodeGen/CatalogEmitter.h"
#include "CodeGen/CppNames.h"
#include "Driver/GenerationRequest.h"
#include "Output/GeneratedFile.h"
#include <set>
#include <string>
#include <utility>
namespace pg::meta::tool
{
GeneratedFile EmitCatalog(const GenerationRequest& request)
{
    std::set<std::string> headers;
    for (const auto& entry : request.Catalog)
    {
        headers.insert(entry.QueryHeader);
    }
    std::string content;
    for (const auto& header : headers)
    {
        content += "#include " + QuoteCppString(header) + "\n";
    }
    content += "#include <GeneratedModuleCatalog.h>\n#include <ModuleDescriptor.h>\n#include <array>\n";
    content += "\nnamespace pg::module\n{\nStaticModuleCatalog GeneratedStaticModuleCatalog() noexcept\n{\n"
               "    static constexpr std::array<StaticCatalogEntry, " +
               std::to_string(request.Catalog.size()) + "> kEntries{\n";
    if (request.Catalog.empty())
    {
        content += "        {},\n";
    }
    else
    {
        content += "        {\n";
        for (const auto& entry : request.Catalog)
        {
            content += "            {" + GlobalName(entry.QuerySymbol) + "},\n";
        }
        content += "        },\n";
    }
    content += "    };\n    return {kEntries};\n}\n} // namespace pg::module\n";
    return {.RelativePath = "Private/StaticModuleCatalog.gen.cpp", .Content = std::move(content)};
}
} // namespace pg::meta::tool
