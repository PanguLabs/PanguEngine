#include "WindowsProfile.h"
#include "JsonSchema.h"
#include "Result.h"
#include "ToolReport.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <expected>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace pg::abi
{
namespace
{
[[nodiscard]] std::string_view Field(const Json& object, std::string_view key)
{
    const auto* value = Find(object, key);
    return value != nullptr && value->is_string() ? Text(*value) : std::string_view{};
}

[[nodiscard]] std::string_view Macro(std::string_view text, std::string_view name)
{
    const auto marker = "#define " + std::string(name) + " ";
    auto offset = text.find(marker);
    if (offset == std::string_view::npos || (offset != 0 && text.at(offset - 1) != '\n'))
    {
        return {};
    }
    offset += marker.size();
    const auto end = text.find_first_of("\r\n", offset);
    return text.substr(offset, end == std::string_view::npos ? end : end - offset);
}

[[nodiscard]] bool Digits(std::string_view text)
{
    return !text.empty() && std::ranges::all_of(text, [](char value) { return value >= '0' && value <= '9'; });
}

[[nodiscard]] bool Version(std::string_view text)
{
    while (true)
    {
        const auto dot = text.find('.');
        if (!Digits(text.substr(0, dot)))
        {
            return false;
        }
        if (dot == std::string_view::npos)
        {
            return true;
        }
        text.remove_prefix(dot + 1);
    }
}

[[nodiscard]] std::string Normalized(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](unsigned char value)
                           { return value == '\\' ? '/' : static_cast<char>(std::tolower(value)); });
    return result;
}

[[nodiscard]] bool ContainsString(const Json& array, std::string_view expected)
{
    return array.is_array() &&
           std::ranges::any_of(array, [expected](const Json& item)
                               { return item.is_string() && Normalized(Text(item)) == Normalized(expected); });
}

[[nodiscard]] Status Reject(FailureReport& report, std::string_view stage, std::string_view detail)
{
    return std::unexpected(report.Add(ToolError::kInvalidProfile, stage, detail));
}

[[nodiscard]] Status CheckArguments(const Json& unit, FailureReport& report)
{
    const auto* arguments = Find(unit, "arguments");
    if (arguments == nullptr || !arguments->is_array() || arguments->empty())
    {
        return Reject(report, "windows_arguments", Field(unit, "source"));
    }
    constexpr std::array kSimple{
        "-O0",
        "-O1",
        "-O2",
        "-O3",
        "-Os",
        "-Oz",
        "-g",
        "-gcodeview",
        "-gline-tables-only",
        "-std=c++23",
        "-fno-exceptions",
        "-fexceptions",
        "-frtti",
        "-fno-rtti",
        "-funsigned-char",
        "-fsigned-char",
        "-m64",
        "-fPIC",
        "-fpic",
        "-fno-omit-frame-pointer",
        "-DNDEBUG",
        "-DNDEBUG=1",
        "-D_DEBUG",
        "-D_DEBUG=1",
        "-D_DLL",
        "-D_DLL=1",
        "-D_MT",
        "-D_MT=1",
        "-D_ITERATOR_DEBUG_LEVEL=0",
        "-D_ITERATOR_DEBUG_LEVEL=1",
        "-D_ITERATOR_DEBUG_LEVEL=2",
    };
    for (std::size_t index = 0; index < arguments->size(); ++index)
    {
        const auto& value = arguments->at(index);
        if (!value.is_string())
        {
            return Reject(report, "windows_arguments", "non-string argument");
        }
        const auto arg = Text(value);
        if (arg == "-I" || arg == "-isystem")
        {
            ++index;
            if (index == arguments->size() || !arguments->at(index).is_string() || Text(arguments->at(index)).empty())
            {
                return Reject(report, "windows_arguments", arg);
            }
        }
        else if (arg == "-Xclang")
        {
            ++index;
            if (index == arguments->size() || !arguments->at(index).is_string())
            {
                return Reject(report, "windows_arguments", arg);
            }
            constexpr std::array kFrontend{
                "--dependent-lib=msvcrt",  "--dependent-lib=msvcrtd",     "--dependent-lib=libcmt",
                "--dependent-lib=libcmtd", "-flto-visibility-public-std", "-gcodeview",
            };
            if (!std::ranges::contains(kFrontend, Text(arguments->at(index))))
            {
                return Reject(report, "windows_arguments", Text(arguments->at(index)));
            }
        }
        else if (!std::ranges::contains(kSimple, arg) && (!arg.starts_with("-I") || arg.size() <= 2) &&
                 (!arg.starts_with("-isystem") || arg.size() <= 8) &&
                 !(arg.starts_with("--target=x86_64-") && arg.contains("-windows-msvc")) &&
                 (!arg.starts_with("-W") || arg.starts_with("-Wl,") || arg.starts_with("-Wa,") ||
                  arg.starts_with("-Wp,")))
        {
            return Reject(report, "windows_arguments", arg);
        }
    }
    return {};
}

struct Environment
{
    std::string Triple;
    std::string SdkId;
    std::string ToolsetRoot;
    std::string SdkRoot;
};

struct RuntimePolicy
{
    std::string_view Crt;
    std::string_view Startup;
    std::string_view Standard;
    std::string_view Support;
    std::string_view Ucrt;
};

[[nodiscard]] const RuntimePolicy& Runtime(bool dynamic, bool debug)
{
    static constexpr std::array<RuntimePolicy, 4> kPolicies{
        {
            {
                .Crt = "md-release",
                .Startup = "msvcrt.lib",
                .Standard = "msvcprt.lib",
                .Support = "vcruntime.lib",
                .Ucrt = "ucrt.lib",
            },
            {
                .Crt = "md-debug",
                .Startup = "msvcrtd.lib",
                .Standard = "msvcprtd.lib",
                .Support = "vcruntimed.lib",
                .Ucrt = "ucrtd.lib",
            },
            {
                .Crt = "mt-release",
                .Startup = "libcmt.lib",
                .Standard = "libcpmt.lib",
                .Support = "libvcruntime.lib",
                .Ucrt = "libucrt.lib",
            },
            {
                .Crt = "mt-debug",
                .Startup = "libcmtd.lib",
                .Standard = "libcpmtd.lib",
                .Support = "libvcruntimed.lib",
                .Ucrt = "libucrtd.lib",
            },
        },
    };
    return kPolicies.at((dynamic ? 0U : 2U) + (debug ? 1U : 0U));
}

[[nodiscard]] Result<Environment> ReadEnvironment(const Json& evidence, FailureReport& report)
{
    const auto* schema = Find(evidence, "schema");
    const auto sdk = Field(evidence, "sdk_version");
    const auto toolset = Field(evidence, "toolset_version");
    const auto triple = Field(evidence, "triple");
    const auto stl = Normalized(Field(evidence, "stl_header"));
    const auto manifest = Normalized(Field(evidence, "sdk_manifest"));
    const auto stlSuffix = "/vc/tools/msvc/" + std::string(toolset) + "/include/yvals_core.h";
    const auto sdkSuffix = "/platforms/uap/" + std::string(sdk) + "/platform.xml";
    if (schema == nullptr || *schema != 1 || Field(evidence, "target").empty() || Field(evidence, "compiler").empty() ||
        !Version(sdk) || !Version(toolset) || !triple.starts_with("x86_64-pc-windows-msvc") ||
        !stl.ends_with(stlSuffix) || !manifest.ends_with(sdkSuffix) ||
        !Field(evidence, "compiler_banner").contains("https://github.com/llvm/llvm-project"))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_environment"));
    }
    const auto toolsetRoot = stl.substr(0, stl.size() - std::string_view("/include/yvals_core.h").size());
    const auto sdkRoot = manifest.substr(0, manifest.size() - sdkSuffix.size());
    const auto* headers = Find(evidence, "sdk_headers");
    if (headers == nullptr ||
        !ContainsString(*headers, sdkRoot + "/include/" + std::string(sdk) + "/shared/sdkddkver.h") ||
        !ContainsString(*headers, sdkRoot + "/include/" + std::string(sdk) + "/ucrt/corecrt.h"))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_header_sources"));
    }
    return Environment{
        .Triple = std::string(triple),
        .SdkId = "windows-sdk-" + std::string(sdk) + "-msvc-" + std::string(toolset),
        .ToolsetRoot = toolsetRoot,
        .SdkRoot = sdkRoot + "/lib/" + std::string(sdk),
    };
}

[[nodiscard]] Result<Json> ProfileForUnit(const Json& evidence, const Json& unit, const Environment& environment,
                                          FailureReport& report)
{
    if (const auto checked = CheckArguments(unit, report); !checked)
    {
        return std::unexpected(checked.error());
    }
    const auto macros = Field(unit, "macros");
    const auto major = Macro(macros, "__clang_major__");
    const auto minor = Macro(macros, "__clang_minor__");
    const auto patch = Macro(macros, "__clang_patchlevel__");
    const auto stl = Macro(macros, "_MSVC_STL_VERSION");
    auto update = Macro(macros, "_MSVC_STL_UPDATE");
    if (update.ends_with('L'))
    {
        update.remove_suffix(1);
    }
    const auto iterator = Macro(macros, "_ITERATOR_DEBUG_LEVEL");
    const auto rtti = Macro(macros, "_CPPRTTI");
    const auto dll = Macro(macros, "_DLL");
    const bool debug = Macro(macros, "_DEBUG") == "1";
    const auto cpuMarker = Macro(macros, "_M_X64");
    // v1 has no separate MSVC STL ABI-configuration field. Pin the observed
    // vendor defaults instead of folding distinct std::exception/CRT layouts
    // into one fingerprint. _HAS_EXCEPTIONS is a header ABI switch, independent
    // of the compiler's -fno-exceptions code-generation policy.
    if (Macro(macros, "_HAS_EXCEPTIONS") != "1" || Macro(macros, "_HAS_STATIC_RTTI") != "1" ||
        Macro(macros, "_CRT_PACKING") != "8")
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_stl_abi_config", Field(unit, "source")));
    }
    if (major != "23" || !Digits(minor) || !Digits(patch) || !Digits(stl) || !Digits(update) ||
        Macro(macros, "__cplusplus") != "202302L" || Macro(macros, "__SIZEOF_POINTER__") != "8" ||
        Macro(macros, "__SIZEOF_WCHAR_T__") != "2" || Macro(macros, "_WIN32") != "1" || cpuMarker != "100" ||
        Macro(macros, "__BYTE_ORDER__") != "__ORDER_LITTLE_ENDIAN__" || Macro(macros, "_MT") != "1" ||
        (!dll.empty() && dll != "1") || (!rtti.empty() && rtti != "1") ||
        (iterator != "0" && iterator != "1" && iterator != "2") || !Macro(macros, "__EXCEPTIONS").empty() ||
        !Macro(macros, "__cpp_exceptions").empty() || !Macro(macros, "_CPPUNWIND").empty() ||
        !Macro(macros, "__SSE4_2__").empty() || !Macro(macros, "__AVX__").empty())
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_macros", Field(unit, "source")));
    }
    const auto compilerVersion = std::string(major) + "." + std::string(minor) + "." + std::string(patch);
    if (!Field(evidence, "compiler_banner").contains("clang version " + compilerVersion) ||
        !Field(unit, "driver").contains(R"("-triple" ")" + environment.Triple + "\""))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_frontend", Field(unit, "source")));
    }
    const bool dynamic = dll == "1";
    const auto& runtime = Runtime(dynamic, debug);
    const auto directives = Normalized(Field(unit, "directives"));
    if (!directives.contains(std::string("/defaultlib:") + std::string(runtime.Startup)) ||
        !directives.contains(std::string("/defaultlib:") + std::string(runtime.Standard)))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_object_runtime", Field(unit, "object")));
    }
    const auto* libraries = Find(evidence, "runtime_libraries");
    if (libraries == nullptr ||
        !ContainsString(*libraries, environment.ToolsetRoot + "/lib/x64/" + std::string(runtime.Startup)) ||
        !ContainsString(*libraries, environment.ToolsetRoot + "/lib/x64/" + std::string(runtime.Standard)) ||
        !ContainsString(*libraries, environment.ToolsetRoot + "/lib/x64/" + std::string(runtime.Support)) ||
        !ContainsString(*libraries, environment.SdkRoot + "/ucrt/x64/" + std::string(runtime.Ucrt)) ||
        !ContainsString(*libraries, environment.SdkRoot + "/um/x64/kernel32.lib"))
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_runtime_sources"));
    }
    return Json{
        {"schema", 1},
        {"policy", "strict-v1"},
        {
            "target",
            {
                {"triple", environment.Triple},
                {"object", "coff"},
                {"pointer_bits", "64"},
                {"endian", "little"},
                {"cpu_abi", "baseline-x86_64"},
            },
        },
        {
            "compiler",
            {{"id", "clang"}, {"distribution", "llvm-upstream"}, {"version", compilerVersion}, {"cxx_abi", "msvc"}},
        },
        {
            "language",
            {
                {"standard", "c++23"},
                {"exceptions", "off"},
                {"rtti", rtti.empty() ? "off" : "on"},
                {
                    "abi_options",
                    {
                        "bitfield_layout=msvc",
                        "calling_convention=msvc",
                        Macro(macros, "__CHAR_UNSIGNED__").empty() ? "char_signedness=signed"
                                                                   : "char_signedness=unsigned",
                        "enum_width=native",
                        "float_abi=default",
                        "struct_pack=default",
                        "vector_abi=default",
                        "wchar_width=16",
                    },
                },
            },
        },
        {
            "stdlib",
            {
                {"id", "msvc-stl"},
                {"version", std::string(stl) + "." + std::string(update)},
                {"abi_version", "msvc-stl-" + std::string(stl)},
                {"abi_namespace", "na"},
                {"iterator_debug_level", iterator},
            },
        },
        {
            "runtime",
            {
                {"crt", runtime.Crt},
                {"cxx_runtime", dynamic ? "msvc-stl-dynamic" : "msvc-stl-static"},
                {"cxxabi_runtime", "msvc"},
                {"sdk_abi_id", environment.SdkId},
            },
        },
    };
}
} // namespace

Result<Json> BuildWindowsProfile(const Json& evidence, FailureReport& report)
{
    const auto environment = ReadEnvironment(evidence, report);
    if (!environment)
    {
        return std::unexpected(environment.error());
    }
    const auto* units = Find(evidence, "units");
    if (units == nullptr || !units->is_array() || units->empty())
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_units"));
    }
    std::set<std::string> objects;
    std::optional<Json> baseline;
    for (const auto& unit : *units)
    {
        const auto object = Field(unit, "object");
        if (object.empty() || Field(unit, "source").empty() || !objects.insert(Normalized(object)).second)
        {
            return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_object_identity", object));
        }
        auto profile = ProfileForUnit(evidence, unit, *environment, report);
        if (!profile)
        {
            return std::unexpected(profile.error());
        }
        if (baseline && *baseline != *profile)
        {
            return std::unexpected(
                report.Add(ToolError::kInvalidProfile, "windows_tu_disagreement", Field(unit, "source")));
        }
        baseline = std::move(*profile);
    }
    if (!baseline)
    {
        return std::unexpected(report.Add(ToolError::kInvalidProfile, "windows_units"));
    }
    return std::move(*baseline);
}
} // namespace pg::abi
