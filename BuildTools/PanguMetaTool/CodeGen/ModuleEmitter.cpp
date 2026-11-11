#include "CodeGen/ModuleEmitter.h"
#include "CodeGen/CppNames.h"
#include "CodeGen/TypeEmitter.h"
#include "Driver/GenerationRequest.h"
#include "Output/GeneratedFile.h"
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>
namespace pg::meta::tool
{
std::vector<GeneratedFile> EmitModule(const GenerationRequest& request, std::span<const HeaderEmission> headers)
{
    const auto target = EscapeIdentifier(request.Target);
    const std::string naming = "// 符号按 UTF-8 字节单射编码，必须保留转义拼写。\n// NOLINT"
                               "BEGIN(readability-identifier-naming)\n";
    const std::string declaration =
        "#pragma once\n\n#include <ModuleMetaProvider.h> // IWYU pragma: export\n\n" + naming +
        "namespace pg::meta::generated::" + target +
        "\n{\nextern const ::pg::module::ModuleMetaProvider kModuleMetadata;\n} // namespace pg::meta::generated::" +
        target +
        "\n// "
        "NOLINT"
        "END(readability-identifier-naming)\n";
    std::string implementation = "#include \"ModuleMeta.gen.h\"\n";
    std::string entries;
    std::size_t count = 0;
    for (const auto& header : headers)
    {
        if (!header.Description)
        {
            continue;
        }
        const auto include = header.Declaration.lexically_relative("Private").generic_u8string();
        implementation += "#include \"" + std::string(include.begin(), include.end()) + "\"\n";
        for (const auto* type : header.Types)
        {
            ++count;
            const auto infoPrefix = "        .Info = &::pg::meta::generated::" + target + "::";
            const auto infoName = "kInfo_" + EscapeIdentifier(type->QualifiedName) + ",\n";
            // 与仓库 ColumnLimit=120 对齐，长限定名在命名空间边界续行。
            constexpr std::size_t kColumnLimit = 120;
            const bool wrapInfo = infoPrefix.size() + infoName.size() - 1 > kColumnLimit;
            entries += "    ::pg::module::ModuleMetaEntry{\n        .CppType = &typeid(" +
                       GlobalName(type->QualifiedName) + "),\n";
            entries += infoPrefix;
            if (wrapInfo)
            {
                entries += "\n                    ";
            }
            entries += infoName;
            entries += "    },\n";
        }
    }
    implementation +=
        "#include \"" + request.Module.IdentityHeader + "\"\n#include <array>\n\n" + naming +
        "namespace pg::meta::generated::" + target +
        "\n{\nnamespace\n{\nconstexpr ::std::array<::pg::module::ModuleMetaEntry, " + std::to_string(count) +
        "> kEntries{\n" + entries +
        "};\n} // namespace\nconstinit const ::pg::module::ModuleMetaProvider kModuleMetadata{\n    .Owner = " +
        GlobalName(request.Module.IdSymbol) +
        ",\n    .Entries = kEntries,\n};\n} // namespace pg::meta::generated::" + target +
        "\n// NOLINT"
        "END(readability-identifier-naming)\n";
    return {
        {.RelativePath = "Private/ModuleMeta.gen.h", .Content = declaration},
        {.RelativePath = "Private/ModuleMeta.gen.cpp", .Content = std::move(implementation)},
    };
}
} // namespace pg::meta::tool
