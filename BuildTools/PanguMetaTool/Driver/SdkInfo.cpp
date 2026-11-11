#include "Driver/SdkInfo.h"
#include "Driver/Diagnostic.h"
#include <clang/Basic/ParsedAttrInfo.h>
#include <clang/Basic/Version.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <typeinfo>
#include <vector>

namespace pg::meta::tool
{
namespace
{
// FrontendAction 的虚析构与 RTTI 来自 SDK，派生对象不能只靠本地头文件完成链接。
class SdkAction final : public clang::SyntaxOnlyAction
{
};
} // namespace

ToolResult<SdkInfo> CheckSdk()
{
    // 对 SDK 内部创建的对象执行 typeid，避免本地派生类自行产生 RTTI 掩盖缺失。
    const auto builtins = clang::ParsedAttrInfo::getAllBuiltin();
    if (builtins.empty())
    {
        return std::unexpected(Diagnostics{
            {
                .Code = "incompatible_sdk",
                .Message = "Clang SDK has no builtin attributes",
            },
        });
    }
    const auto* builtin = builtins.front();
    const std::type_info& builtinType = typeid(*builtin);
    if (std::string_view(builtinType.name()).empty())
    {
        return std::unexpected(Diagnostics{
            {
                .Code = "sdk_rtti_required",
                .Message = "Clang SDK object RTTI check failed",
            },
        });
    }
    const auto action = std::make_unique<SdkAction>();
    const clang::FrontendAction& base = *action;
    if (typeid(base) != typeid(SdkAction) || dynamic_cast<const SdkAction*>(&base) == nullptr)
    {
        return std::unexpected(Diagnostics{
            {
                .Code = "sdk_rtti_required",
                .Message = "Clang SDK RTTI check failed",
            },
        });
    }
    const std::filesystem::path resourceDir{PANGU_META_RESOURCE_PATH};
    std::error_code error;
    if (!std::filesystem::is_regular_file(resourceDir / "include" / "stddef.h", error) || error)
    {
        return std::unexpected(Diagnostics{
            {
                .Code = "incompatible_sdk",
                .Message = "Clang resource include/stddef.h is missing",
            },
        });
    }
    // 同时跨 SDK 传递拥有型字符串/容器并解析资源头，验证实际标准库 ABI 和前端。
    const std::vector<std::string> args{
        "-std=c++23", "-fsyntax-only", "-fno-exceptions", "-frtti", "-Werror", "-resource-dir", resourceDir.string(),
    };
    if (!clang::tooling::runToolOnCodeWithArgs(std::make_unique<SdkAction>(),
                                               "#include <stddef.h>\nstruct Value { int Number; };\n"
                                               "static_assert(sizeof(Value) >= sizeof(int));\n",
                                               args, "PanguMetaSdk.cpp", PANGU_META_HOST_COMPILER_PATH))
    {
        return std::unexpected(Diagnostics{
            {
                .Code = "incompatible_sdk",
                .Message = "Clang SDK could not parse a raw C++ translation unit",
            },
        });
    }
#ifdef _WIN32
    constexpr auto kHostStdlib = "msvc";
#else
    constexpr auto kHostStdlib = "libc++";
#endif
    return SdkInfo{
        .FrontendVersion = CLANG_VERSION_STRING,
        .ResourceDir = resourceDir,
        .HasRtti = true,
        .HostStdlib = kHostStdlib,
    };
}
} // namespace pg::meta::tool
