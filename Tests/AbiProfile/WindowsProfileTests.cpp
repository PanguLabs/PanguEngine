#include "JsonSchema.h"
#include "ToolReport.h"
#include "WindowsProfile.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>

namespace pg::abi
{
namespace
{
[[nodiscard]] Json Evidence()
{
    // Synthetic private evidence exercises mapping only, never provenance attestation.
    const std::string macros =
        "#define __clang_major__ 23\n#define __clang_minor__ 1\n#define __clang_patchlevel__ 0\n"
        "#define __cplusplus 202302L\n#define __SIZEOF_POINTER__ 8\n#define __SIZEOF_WCHAR_T__ 2\n"
        "#define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__\n#define _WIN32 1\n#define _M_X64 100\n"
        "#define _MSVC_STL_VERSION 145\n#define _MSVC_STL_UPDATE 202604L\n"
        "#define _ITERATOR_DEBUG_LEVEL 0\n#define _DLL 1\n#define _MT 1\n#define _CPPRTTI 1\n"
        "#define _HAS_EXCEPTIONS 1\n#define _HAS_STATIC_RTTI 1\n#define _CRT_PACKING 8\n";
    const Json unit{
        {"source", "D:/source/First.cpp"},
        {"object", "D:/build/First.obj"},
        {"arguments", {"-O3", "-DNDEBUG", "-std=c++23", "-D_DLL", "-D_MT", "-fno-exceptions"}},
        {"macros", macros},
        {"driver", R"("clang" "-cc1" "-triple" "x86_64-pc-windows-msvc19.51.36248")"},
        {"directives", "/DEFAULTLIB:msvcrt.lib /DEFAULTLIB:msvcprt.lib"},
        {"headers", "synthetic"},
    };
    Json second = unit;
    second.at("source") = "D:/source/Second.cpp";
    second.at("object") = "D:/build/Second.obj";
    return {
        {"schema", 1},
        {"target", "Fixture"},
        {"compiler", "D:/LLVM/bin/clang++.exe"},
        {"compiler_banner", "clang version 23.1.0 (https://github.com/llvm/llvm-project fixture)"},
        {"triple", "x86_64-pc-windows-msvc19.51.36248"},
        {"sdk_version", "10.0.26100.0"},
        {"toolset_version", "14.51.36231"},
        {"stl_header", "C:/VS/VC/Tools/MSVC/14.51.36231/include/yvals_core.h"},
        {"sdk_manifest", "C:/SDK/Platforms/UAP/10.0.26100.0/Platform.xml"},
        {
            "sdk_headers",
            {"C:/SDK/Include/10.0.26100.0/shared/sdkddkver.h", "C:/SDK/Include/10.0.26100.0/ucrt/corecrt.h"},
        },
        {
            "runtime_libraries",
            {
                "C:/VS/VC/Tools/MSVC/14.51.36231/lib/x64/msvcrt.lib",
                "C:/VS/VC/Tools/MSVC/14.51.36231/lib/x64/msvcprt.lib",
                "C:/VS/VC/Tools/MSVC/14.51.36231/lib/x64/vcruntime.lib",
                "C:/SDK/Lib/10.0.26100.0/ucrt/x64/ucrt.lib",
                "C:/SDK/Lib/10.0.26100.0/um/x64/kernel32.lib",
            },
        },
        {"units", Json::array({unit, second})},
    };
}

void ReplaceMacro(Json& evidence, std::string_view from, std::string_view to)
{
    auto& value = evidence.at("units").at(1).at("macros").get_ref<std::string&>();
    value.replace(value.find(from), from.size(), to);
}

int Main(const std::filesystem::path& schemaPath)
{
    int failures = 0;
    const auto expect = [&failures](bool condition, std::string_view label)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "FAIL: " << label << '\n';
        }
    };
    std::ifstream input(schemaPath);
    const auto schema = ParseUnique(std::string(std::istreambuf_iterator<char>(input), {}));
    FailureReport report;
    const auto baseline = BuildWindowsProfile(Evidence(), report);
    auto projectMacros = Evidence();
    for (auto& unit : projectMacros.at("units"))
    {
        unit.at("arguments").push_back("-DJSON_USE_GLOBAL_UDLS=0");
        unit.at("arguments").push_back("-DPANGU_ENABLE_PROFILING=1");
    }
    FailureReport projectReport;
    const auto projectProfile = BuildWindowsProfile(projectMacros, projectReport);
    expect(projectProfile && baseline && *projectProfile == *baseline, "ordinary project macros accepted");
    expect(projectMacros.at("units").at(0).at("arguments").at(6) == "-DJSON_USE_GLOBAL_UDLS=0" &&
               projectMacros.at("units").at(0).at("arguments").at(7) == "-DPANGU_ENABLE_PROFILING=1",
           "ordinary project macros retained in evidence");
    expect(baseline && schema && Validate(*schema, *baseline), "complete two-TU profile conforms to frozen schema");
    if (baseline)
    {
        expect(baseline->at("runtime").at("crt") == "md-release", "STATIC can use dynamic CRT");
        expect(baseline->at("stdlib").at("version") == "145.202604", "STL version uses header macros");
    }
    auto debugInfoEvidence = Evidence();
    for (auto& unit : debugInfoEvidence.at("units"))
    {
        unit.at("arguments").push_back("-Xclang");
        unit.at("arguments").push_back("-gcodeview");
    }
    FailureReport debugInfoReport;
    const auto debugInfo = BuildWindowsProfile(debugInfoEvidence, debugInfoReport);
    expect(debugInfo && baseline && *debugInfo == *baseline, "CodeView debug metadata does not alter ABI profile");
    auto allRttiOff = Evidence();
    for (auto& unit : allRttiOff.at("units"))
    {
        auto& macros = unit.at("macros").get_ref<std::string&>();
        const std::string marker = "#define _CPPRTTI 1\n";
        macros.erase(macros.find(marker), marker.size());
        unit.at("arguments").push_back("-fno-rtti");
    }
    FailureReport rttiReport;
    const auto rttiOff = BuildWindowsProfile(allRttiOff, rttiReport);
    expect(rttiOff && rttiOff->at("language").at("rtti") == "off", "consistent RTTI-off target supported");
    auto allUnsigned = Evidence();
    for (auto& unit : allUnsigned.at("units"))
    {
        unit.at("macros").get_ref<std::string&>() += "#define __CHAR_UNSIGNED__ 1\n";
        unit.at("arguments").push_back("-funsigned-char");
    }
    FailureReport charReport;
    const auto unsignedChar = BuildWindowsProfile(allUnsigned, charReport);
    expect(unsignedChar && unsignedChar->at("language").at("abi_options").at(2) == "char_signedness=unsigned",
           "effective unsigned char changes profile");
    const auto rejects = [&expect](const Json& evidence, std::string_view label)
    {
        FailureReport failure;
        const auto profile = BuildWindowsProfile(evidence, failure);
        expect(!profile && failure.Count != 0, label);
    };
    auto changed = Evidence();
    ReplaceMacro(changed, "_CPPRTTI 1", "");
    rejects(changed, "RTTI conflict in second TU rejected");
    changed = Evidence();
    ReplaceMacro(changed, "_ITERATOR_DEBUG_LEVEL 0", "_ITERATOR_DEBUG_LEVEL 2");
    rejects(changed, "iterator conflict in second TU rejected");
    changed = Evidence();
    changed.at("units").at(1).at("arguments").push_back("-fpack-struct=1");
    rejects(changed, "unsupported layout flag rejected");
    changed = Evidence();
    changed.at("units").at(1).at("arguments").push_back("-D_MSVC_STL_VERSION=145");
    rejects(changed, "spoofed STL macro rejected");
    changed = Evidence();
    changed.at("units").at(1).at("object") = changed.at("units").at(0).at("object");
    rejects(changed, "duplicate object rejected");
    changed = Evidence();
    changed.at("units") = Json::array();
    rejects(changed, "empty target rejected");
    changed = Evidence();
    changed.at("runtime_libraries").at(0) = "C:/other/msvcrt.lib";
    rejects(changed, "runtime outside selected toolset rejected");
    changed = Evidence();
    changed.at("sdk_version") = "10.0.99999.0";
    rejects(changed, "SDK version/source disagreement rejected");
    changed = Evidence();
    changed.at("units").at(1).at("driver") = R"("-triple" "aarch64-pc-windows-msvc")";
    rejects(changed, "frontend target disagreement rejected");
    changed = Evidence();
    changed.at("units").at(1).at("macros") =
        changed.at("units").at(1).at("macros").get<std::string>() + "#define __cpp_exceptions 199711L\n";
    rejects(changed, "enabled exceptions rejected");
    changed = Evidence();
    changed.at("units").at(1).at("directives") = "/DEFAULTLIB:libcmt.lib";
    rejects(changed, "COFF/runtime policy disagreement rejected");
    for (const auto* conflicting : std::array{
             "msvcrtd.lib",
             "libcmt.lib",
             "libcmt",
             "libcmtd.lib",
             "msvcprtd.lib",
             "libcpmt.lib",
             "libcpmtd.lib",
             "vcruntimed.lib",
             "libvcruntime.lib",
             "libvcruntimed.lib",
             "ucrtd.lib",
             "libucrt.lib",
             "libucrtd.lib",
         })
    {
        changed = Evidence();
        changed.at("units").at(1).at("directives").get_ref<std::string&>() +=
            " /DEFAULTLIB:" + std::string(conflicting);
        rejects(changed, "additional conflicting CRT directive rejected");
    }
    for (const auto* spoofed : std::array{
             "/DEFAULTLIB:msvcrt.lib.evil /DEFAULTLIB:msvcprt.lib",
             "/DEFAULTLIB:msvcrt.lib /DEFAULTLIB:msvcprt.lib.evil",
             R"(/DEFAULTLIB:msvcrt.lib /DEFAULTLIB:msvcprt.lib /DEFAULTLIB:"C:\rogue\msvcrt.lib")",
             "/DEFAULTLIB:msvcrt.lib /DEFAULTLIB:msvcprt.lib /DEFAULTLIB:C:msvcrt.lib",
             "/DEFAULTLIB:msvcrt.lib /DEFAULTLIB:msvcprt.lib /DEFAULTLIB:\"C:/rogue/libcmt.lib\"",
             "/DEFAULTLIB:msvcrt.lib /DEFAULTLIB:msvcprt.lib /NODEFAULTLIB:msvcrt.lib",
             "/DEFAULTLIB:msvcrt.lib /DEFAULTLIB:msvcprt.lib /FAILIFMISMATCH:\"RuntimeLibrary=MT_StaticRelease\"",
         })
    {
        changed = Evidence();
        changed.at("units").at(1).at("directives") = spoofed;
        rejects(changed, "CRT directive token or provider spoof rejected");
    }
    changed = Evidence();
    changed.at("units").at(1).at("directives") =
        R"(/defaultlib:"MSVCRT.LIB" -DEFAULTLIB:MSVCPRT.LIB /DEFAULTLIB:"User library.lib")";
    FailureReport quotedReport;
    expect(BuildWindowsProfile(changed, quotedReport).has_value(), "quoted case-insensitive CRT tokens accepted");
    changed = Evidence();
    changed.at("units").at(1).at("directives") = "/DEFAULTLIB:msvcrt /DEFAULTLIB:msvcprt";
    FailureReport extensionlessReport;
    expect(BuildWindowsProfile(changed, extensionlessReport).has_value(), "extensionless CRT names accepted");
    rejects(Json::object(), "missing evidence fields rejected");
    changed = Evidence();
    changed.at("units").at(1).at("arguments") = Json::array({42});
    rejects(changed, "non-string compiler argument rejected");
    changed = Evidence();
    ReplaceMacro(changed, "_MSVC_STL_UPDATE 202604L", "_MSVC_STL_UPDATE 202605L");
    rejects(changed, "different STL header update in second TU rejected");
    changed = Evidence();
    ReplaceMacro(changed, "_HAS_EXCEPTIONS 1", "_HAS_EXCEPTIONS 0");
    rejects(changed, "unrepresented STL exception ABI variant rejected");
    changed = Evidence();
    ReplaceMacro(changed, "_CRT_PACKING 8", "_CRT_PACKING 1");
    rejects(changed, "unrepresented CRT packing override rejected");
    return failures == 0 ? 0 : 1;
}
} // namespace
} // namespace pg::abi

int main(int argc, char* const* argv)
{
    return argc == 2 ? pg::abi::Main(std::span(argv, static_cast<std::size_t>(argc)).subspan<1>().front()) : 2;
}
