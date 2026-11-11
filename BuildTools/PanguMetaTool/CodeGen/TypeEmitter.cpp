#include "CodeGen/TypeEmitter.h"
#include "CodeGen/CppNames.h"
#include "Driver/GenerationRequest.h"
#include "MetaIR/Model.h"
#include "MetaIdentity.h"
#include "MetaOptions.h"
#include "Output/GeneratedFile.h"
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pg::meta::tool
{
namespace
{
const char* Boolean(bool value) { return value ? "true" : "false"; }
std::string Id(const std::optional<TypeId>& id, const std::string& indent)
{
    if (!id)
    {
        return "::std::nullopt";
    }
    std::string result = "::pg::meta::TypeId{\n" + indent + "    .Bytes =\n" + indent + "        {\n";
    for (const auto byte : id->Bytes)
    {
        result += indent + "            ::std::byte{" + std::to_string(std::to_integer<unsigned>(byte)) + "},\n";
    }
    return result + indent + "        },\n" + indent + "}";
}
std::string Field(const ir::FieldDefinition& field, const std::string& indent)
{
    std::string access = "kPublic";
    if (field.Visibility == Access::kPrivate)
    {
        access = "kPrivate";
    }
    if (field.Visibility == Access::kProtected)
    {
        access = "kProtected";
    }
    return "::pg::meta::FieldInfo{\n" + indent + "    .Name = " + QuoteCppString(field.Name) + ",\n" + indent +
           "    .Key = " + QuoteCppString(field.Key) + ",\n" + indent +
           "    .TypeName = " + QuoteCppString(field.CppType) + ",\n" + indent +
           "    .Visibility = ::pg::meta::Access::" + access + ",\n" + indent +
           "    .IsConst = " + Boolean(field.IsConst) + ",\n" + indent +
           "    .IsReadOnly = " + Boolean(field.ReadOnly) + ",\n" + indent + "}";
}
std::string Utf8(const std::filesystem::path& path)
{
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}
} // namespace
std::vector<GeneratedFile> EmitTypes(const GenerationRequest& request, const HeaderEmission& header)
{
    const auto target = EscapeIdentifier(request.Target);
    std::string declaration = "#pragma once\n\n#include \"" + header.OriginalInclude +
                              "\" // IWYU pragma: export\n#include \"" + request.Module.IdentityHeader +
                              "\"\n#include <MetaTraits.h> // IWYU pragma: export\n\nnamespace pg::meta\n{\n";
    for (const auto* type : header.Types)
    {
        declaration += "template <>\nstruct TypeTraits<" + GlobalName(type->QualifiedName) + ">\n{\n";
        // 公共 Traits 协议的成员名固定，不随变量命名规则改变。
        declaration += "    // 公共 Traits 协议固定成员拼写。\n    // NOLINT"
                       "BEGIN(readability-identifier-naming)\n";
        declaration += "    static constexpr auto Owner = " + GlobalName(request.Module.IdSymbol) + ";\n";
        declaration += "    static constexpr ::std::string_view Name = " + QuoteCppString(type->QualifiedName) + ";\n";
        declaration += "    static constexpr ::std::optional<::pg::meta::TypeId> Id = " + Id(type->Id, "    ") + ";\n";
        declaration += std::string("    static constexpr bool HasTypedAccess = ") + Boolean(header.TypedAccess) + ";\n";
        declaration += std::string("    static constexpr bool HasDescription = ") + Boolean(header.Description) + ";\n";
        const bool fields = header.TypedAccess && std::ranges::any_of(type->Fields, [](const auto& field)
                                                                      { return field.Visibility == Access::kPublic; });
        declaration += "    static constexpr ::std::tuple Fields{";
        if (fields)
        {
            declaration += '\n';
        }
        if (header.TypedAccess)
        {
            for (const auto& field : type->Fields)
            {
                if (field.Visibility != Access::kPublic)
                {
                    continue;
                }
                declaration += "        ::pg::meta::FieldAccess<&" + GlobalName(type->QualifiedName) +
                               "::" + field.Name + ", " + Boolean(field.ReadOnly) +
                               ">{\n            .Info =\n                " + Field(field, "                ") +
                               ",\n        },\n";
            }
        }
        if (fields)
        {
            declaration += "    ";
        }
        declaration += "};\n    // NOLINT"
                       "END(readability-identifier-naming)\n};\n";
    }
    declaration += "} // namespace pg::meta\n";
    std::vector<GeneratedFile> result;
    if (header.Description)
    {
        const std::string naming = "// 符号按 UTF-8 字节单射编码，必须保留转义拼写。\n// NOLINT"
                                   "BEGIN(readability-identifier-naming)\n";
        declaration += naming + "namespace pg::meta::generated::" + target + "\n{\n";
        std::string implementation =
            "#include \"" + Utf8(header.Declaration.lexically_relative(header.Implementation.parent_path())) + "\"\n";
        if (std::ranges::any_of(header.Types, [](const auto* type) { return type->Id.has_value(); }))
        {
            implementation += "#include <MetaIdentity.h>\n";
        }
        if (!header.Types.empty())
        {
            implementation += "#include <MetaInfo.h>\n";
        }
        if (std::ranges::any_of(header.Types, [](const auto* type) { return !type->Fields.empty(); }))
        {
            implementation += "#include <MetaOptions.h>\n";
        }
        if (!header.Types.empty())
        {
            implementation += "#include <array>\n";
        }
        if (std::ranges::any_of(header.Types, [](const auto* type) { return type->Id.has_value(); }))
        {
            implementation += "#include <cstddef>\n";
        }
        if (std::ranges::any_of(header.Types, [](const auto* type) { return !type->Id; }))
        {
            implementation += "#include <optional>\n";
        }
        implementation += "\n" + naming + "namespace pg::meta::generated::" + target + "\n{\n";
        for (const auto* type : header.Types)
        {
            const auto symbol = EscapeIdentifier(type->QualifiedName);
            declaration += "extern const ::pg::meta::TypeInfo kInfo_" + symbol + ";\n";
            implementation += "namespace\n{\nconstexpr ::std::array<::pg::meta::FieldInfo, " +
                              std::to_string(type->Fields.size()) + "> kFields_" + symbol + "{\n";
            for (const auto& field : type->Fields)
            {
                implementation += "    " + Field(field, "    ") + ",\n";
            }
            implementation.append("};\n} // namespace\nconstinit const ::pg::meta::TypeInfo kInfo_")
                .append(symbol)
                .append("{\n    .Name = ")
                .append(QuoteCppString(type->QualifiedName))
                .append(type->Id ? ",\n    .Id =\n        " : ",\n    .Id = ")
                .append(Id(type->Id, "        "))
                .append(",\n    .Fields = kFields_")
                .append(symbol)
                .append(",\n};\n");
        }
        implementation += "} // namespace pg::meta::generated::" + target +
                          "\n// NOLINT"
                          "END(readability-identifier-naming)\n";
        declaration += "} // namespace pg::meta::generated::" + target +
                       "\n// NOLINT"
                       "END(readability-identifier-naming)\n";
        result.push_back({.RelativePath = header.Implementation, .Content = std::move(implementation)});
    }
    result.push_back({.RelativePath = header.Declaration, .Content = std::move(declaration)});
    return result;
}
} // namespace pg::meta::tool
