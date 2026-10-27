#include "../../BuildTools/AbiProfile/Private/CollectionRequest.h"
#include "../../BuildTools/AbiProfile/Private/CompileCommands.h"
#include "../../BuildTools/AbiProfile/Private/Process.h"
#include "../../BuildTools/AbiProfile/Private/ToolReport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <system_error>
#endif

namespace
{
using pg::abi::CommandLineStyle;
using pg::abi::FailureReport;
using pg::abi::LoadCompileUnits;
using pg::abi::MatchCompilerInvocation;
using pg::abi::ParseCommandLine;

void Write(const std::filesystem::path& path, std::string_view contents)
{
    std::ofstream file(path, std::ios::binary);
    file << contents;
}

int CheckRealDriver(const std::filesystem::path& root, const std::filesystem::path& compilerPath)
{
    int failures = 0;
    const auto check = [&failures](bool passed, std::string_view label)
    {
        if (!passed)
        {
            ++failures;
            std::cerr << "FAIL: " << label << '\n';
        }
    };
    std::filesystem::create_directories(root);
    Write(root / "driver.cpp", "int DriverProbe;\n");
    pg::abi::CollectionRequest request;
    request.BuildRoot = root;
    request.Compiler = compilerPath;
    request.Database = root / "compile_commands.json";
    request.ObjectManifest = root / "objects.txt";
#ifdef _WIN32
    request.HostPlatform = pg::abi::Platform::kWindows;
    request.CommandLineStyle = CommandLineStyle::kWindows;
#elifdef __APPLE__
    request.HostPlatform = pg::abi::Platform::kMacos;
    request.CommandLineStyle = CommandLineStyle::kPosix;
#else
    request.HostPlatform = pg::abi::Platform::kLinux;
    request.CommandLineStyle = CommandLineStyle::kPosix;
#endif
    request.TargetPlatform = pg::abi::Platform::kWindows;
    const auto compiler = compilerPath.generic_string();
    const auto database = [&](const std::vector<std::string>& values)
    {
        const nlohmann::json entries = nlohmann::json::array({
            {
                {"directory", root.generic_string()},
                {"file", "driver.cpp"},
                {"output", "driver.o"},
                {"arguments", values},
            },
        });
        Write(request.Database, entries.dump());
        Write(request.ObjectManifest, "driver.o\n");
    };
    Write(root / "driver.rsp", R"("-DDOUBLE=a\qb" '-DSINGLE=c\qd')");
    pg::abi::ProcessRequest process;
    process.Program = compilerPath;
    process.Directory = root;
    process.Arguments = {"-E", "-dM", "driver.cpp", "@driver.rsp"};
    FailureReport processReport;
    const auto observed = pg::abi::RunProcess(process, processReport);
    check(observed && observed->ExitCode == 0 && observed->Stdout.contains("#define DOUBLE aqb") &&
              observed->Stdout.contains("#define SINGLE cqd"),
          "real GNU driver consumes quoted backslashes in both quote kinds");
    const std::vector<std::string> rspArguments{compiler, "@driver.rsp", "-c", "driver.cpp", "-o", "driver.o"};
    database(rspArguments);
    FailureReport expansionReport;
    const auto expanded = LoadCompileUnits(request, expansionReport);
    check(expanded && std::ranges::contains(expanded->front().Arguments, "-DDOUBLE=aqb") &&
              std::ranges::contains(expanded->front().Arguments, "-DSINGLE=cqd"),
          "GNU response expansion agrees with the real driver on this Host");
    const std::vector<std::string> unsafeExplicit{
        compiler, "-DDOUBLE=a\\qb", "-DSINGLE=c\\qd", "-c", "driver.cpp", "-o", "driver.o",
    };
    database(unsafeExplicit);
    FailureReport mismatchReport;
    check(!MatchCompilerInvocation(request, rspArguments, root, mismatchReport),
          "explicit macro values differing from real GNU rsp cannot match");
    database({compiler, "-DDOUBLE=aqb", "-DSINGLE=cqd", "-c", "driver.cpp", "-o", "driver.o"});
    FailureReport equivalentReport;
    check(MatchCompilerInvocation(request, rspArguments, root, equivalentReport).has_value(),
          "equivalent explicit argv and GNU response match");

    Write(root / "driver.rsp", R"("-DDOUBLE=a\qb")");
    for (const auto* style : {"posix", "windows"})
    {
        const auto control = std::string("--rsp-quoting=") + style;
        process.Arguments = {control, "-E", "-dM", "driver.cpp", "@driver.rsp"};
        const auto real = pg::abi::RunProcess(process, processReport);
        const std::string expected = std::string_view(style) == "windows" ? "a\\qb" : "aqb";
        check(real && real->ExitCode == 0 && real->Stdout.contains("#define DOUBLE " + expected),
              "real driver explicit rsp-quoting behavior");
        database({compiler, control, "@driver.rsp", "-c", "driver.cpp", "-o", "driver.o"});
        FailureReport explicitReport;
        const auto units = LoadCompileUnits(request, explicitReport);
        check(units && std::ranges::contains(units->front().Arguments, "-DDOUBLE=" + expected),
              "explicit rsp-quoting expansion agrees with real driver");
    }
    database({compiler, "--rsp-quoting=unknown", "@driver.rsp", "-c", "driver.cpp", "-o", "driver.o"});
    FailureReport unknownReport;
    check(!LoadCompileUnits(request, unknownReport), "unsupported explicit rsp-quoting rejected");

    process.Arguments = {"--driver-mode=cl", "--driver-mode=g++", "-E", "-dM", "driver.cpp", "@driver.rsp"};
    const auto lastMode = pg::abi::RunProcess(process, processReport);
    check(lastMode && lastMode->ExitCode == 0 && lastMode->Stdout.contains("#define DOUBLE aqb"),
          "real driver uses last explicit driver mode before rsp expansion");
    database({compiler, "--driver-mode=cl", "--driver-mode=g++", "@driver.rsp", "-c", "driver.cpp", "-o", "driver.o"});
    FailureReport lastModeReport;
    const auto modeUnits = LoadCompileUnits(request, lastModeReport);
    check(modeUnits && modeUnits->front().DriverMode == "g++" &&
              modeUnits->front().ResponseStyle == pg::abi::ResponseFileStyle::kGnu &&
              std::ranges::contains(modeUnits->front().Arguments, "-DDOUBLE=aqb"),
          "last explicit driver mode controls real GNU rsp tokenizer");

#ifndef _WIN32
    auto clPath = root / "clang-cl";
    std::error_code error;
    if (!std::filesystem::exists(clPath, error))
    {
        std::filesystem::create_symlink(compilerPath, clPath, error);
        if (error)
        {
            // If the test filesystem cannot create a symlink, use the installed
            // clang-cl symlink only after checking that it targets this compiler.
            error.clear();
            const auto installedCl = compilerPath.parent_path() / "clang-cl";
            if (std::filesystem::is_symlink(installedCl, error) &&
                std::filesystem::equivalent(installedCl, compilerPath, error) && !error)
            {
                clPath = installedCl;
            }
        }
    }
    check(!error && std::filesystem::is_symlink(clPath), "real clang-cl symlink available");
    if (!error)
    {
        process.Program = clPath;
        process.Arguments = {"-MD", "-###", "/c", "driver.cpp"};
        const auto dynamic = pg::abi::RunProcess(process, processReport);
        process.Arguments = {"-###", "/c", "driver.cpp"};
        const auto defaultCrt = pg::abi::RunProcess(process, processReport);
        check(dynamic && defaultCrt && dynamic->ExitCode == 0 && defaultCrt->ExitCode == 0 &&
                  dynamic->Stderr.contains("--dependent-lib=msvcrt") &&
                  defaultCrt->Stderr.contains("--dependent-lib=libcmt"),
              "real symlink clang-cl -MD changes CRT");
        const auto cl = clPath.generic_string();
        process.Arguments = {"/E", "/clang:-dM", "driver.cpp", "@driver.rsp"};
        const auto clMacros = pg::abi::RunProcess(process, processReport);
        check(clMacros && clMacros->ExitCode == 0 && clMacros->Stdout.contains("#define DOUBLE a\\qb"),
              "real symlink clang-cl defaults to Windows rsp tokenizer");
        database({cl, "@driver.rsp", "/c", "driver.cpp", "/Fodriver.o"});
        FailureReport clExpansionReport;
        const auto clUnits = LoadCompileUnits(request, clExpansionReport);
        check(clUnits && clUnits->front().DriverName == "clang-cl" && clUnits->front().DriverMode == "cl" &&
                  clUnits->front().ResponseStyle == pg::abi::ResponseFileStyle::kWindows &&
                  std::ranges::contains(clUnits->front().Arguments, "-DDOUBLE=a\\qb"),
              "symlink invocation driver semantics survive canonical compiler identity");
        database({cl, "-MD", "/c", "driver.cpp", "/Fodriver.o"});
        FailureReport clReport;
        check(!MatchCompilerInvocation(request, std::vector<std::string>{cl, "/c", "driver.cpp", "/Fodriver.o"}, root,
                                       clReport),
              "canonical clang-cl symlink identity cannot erase CRT -MD difference");
    }
#endif
    std::cout << "real driver checks: " << compiler << " failures=" << failures << '\n';
    return failures;
}
} // namespace

int main(int argc, char* const* argv)
{
    if (argc != 3)
    {
        return 2;
    }
    const auto args = std::span(argv, static_cast<std::size_t>(argc));
    const auto root = std::filesystem::absolute(args.subspan(1).front()) / "Compile commands space";
    std::filesystem::create_directories(root / "nested");
    int failures = 0;
    const auto expect = [&failures](bool passed, std::string_view label)
    {
        if (!passed)
        {
            ++failures;
            std::cerr << "FAIL: " << label << '\n';
        }
    };
    const auto quoting =
        [&expect](std::string_view input, CommandLineStyle style, const std::vector<std::string>& expected)
    {
        FailureReport report;
        const auto actual = ParseCommandLine(input, style, report);
        expect(actual && *actual == expected, "literal quoting argv");
    };
    quoting(R"(clang++ 'a b' "c d" '' a\ b "quote\"inside" "back\\slash" '$x;|')", CommandLineStyle::kPosix,
            {"clang++", "a b", "c d", "", "a b", "quote\"inside", "back\\slash", "$x;|"});
    quoting(R"(C:\LLVM\clang++.exe "a b" "" "quote\"inside" "end\\" a\b $x;|)", CommandLineStyle::kWindows,
            {R"(C:\LLVM\clang++.exe)", "a b", "", "quote\"inside", "end\\", "a\\b", "$x;|"});
    for (const auto style : {CommandLineStyle::kPosix, CommandLineStyle::kWindows})
    {
        FailureReport report;
        expect(!ParseCommandLine("clang++ \"unfinished", style, report), "unclosed quote rejected");
    }
    pg::abi::CollectionRequest request;
    request.BuildRoot = root;
    request.SourceRoot = root;
    request.Compiler = root / "clang++";
    request.Database = root / "compile_commands.json";
    request.ObjectManifest = root / "objects.txt";
    request.HostPlatform = pg::abi::Platform::kLinux;
    request.TargetPlatform = pg::abi::Platform::kWindows; // Host quoting is independent of target ABI.
    request.CommandLineStyle = CommandLineStyle::kPosix;
    const auto compiler = request.Compiler.generic_string();
    const std::vector<std::string> first{compiler, "-DJSON_USE_GLOBAL_UDLS=0", "-c", "shared.cpp", "-o", "A.o"};
    const std::vector<std::string> second{compiler, "@nested/outer.rsp", "-c", "second.cpp", "-o", "B.o"};
    const nlohmann::json database = nlohmann::json::array({
        {{"directory", root.generic_string()}, {"file", "shared.cpp"}, {"output", "A.o"}, {"arguments", first}},
        {{"directory", root.generic_string()}, {"file", "second.cpp"}, {"output", "B.o"}, {"arguments", second}},
        {
            {"directory", root.generic_string()},
            {"file", "shared.cpp"},
            {"output", "Other.o"},
            {"arguments", {compiler, "-fno-rtti", "-c", "shared.cpp", "-o", "Other.o"}},
        },
    });
    Write(request.Database, database.dump());
    Write(request.ObjectManifest, "./A.o\nB.o\n");
    Write(root / "nested/outer.rsp", "@inner.rsp -DPANGU_ENABLE_PROFILING=1");
    Write(root / "inner.rsp", "-DQUOTED='hello world'");
    FailureReport report;
    const auto units = LoadCompileUnits(request, report);
    expect(units && units->size() == 2, "exact two outputs selected despite shared source");
    if (units && units->size() == 2)
    {
        expect(units->front().OriginalArguments == first && units->at(1).OriginalArguments == second,
               "original argv retained");
        expect(units->at(1).Arguments ==
                   std::vector<std::string>{
                       compiler,
                       "-DQUOTED=hello world",
                       "-DPANGU_ENABLE_PROFILING=1",
                       "-c",
                       "second.cpp",
                       "-o",
                       "B.o",
                   },
               "expanded argv retained");
        expect(units->at(1).ResponseFiles ==
                   std::vector<std::filesystem::path>{root / "nested/outer.rsp", root / "inner.rsp"},
               "nested response paths resolved from original compile cwd");
    }
    auto actual = first;
    actual.insert(actual.end(), {"-MD", "-MT", "A.o", "-MF", "A.d"});
    const auto matched = MatchCompilerInvocation(request, actual, root, report);
    expect(matched && matched->OriginalArguments == actual, "dependency output controls do not create false conflict");
    actual.emplace_back("-funknown-abi-switch");
    expect(!MatchCompilerInvocation(request, actual, root, report), "unknown flag is retained and conflicts");
    actual = first;
    actual.at(1) = "-DJSON_USE_GLOBAL_UDLS=1";
    expect(!MatchCompilerInvocation(request, actual, root, report), "ordinary macro change conflicts");
    auto aliasRequest = request;
    aliasRequest.Compiler = root / "nested/../clang++";
    FailureReport aliasReport;
    expect(LoadCompileUnits(aliasRequest, aliasReport).has_value(), "compiler path aliases normalized");
    const auto rejects = [&expect, &request](std::string_view label)
    {
        FailureReport failure;
        expect(!LoadCompileUnits(request, failure) && failure.Count > 0, label);
    };
    Write(request.ObjectManifest, "");
    rejects("empty manifest rejected");
    Write(request.ObjectManifest, "A.o\n./A.o\n");
    rejects("duplicate manifest output rejected");
    Write(request.ObjectManifest, "Missing.o\n");
    rejects("unmatched output rejected");
    Write(request.ObjectManifest, "A.o\nB.o\n");
    auto changed = database;
    changed.push_back(database.front());
    Write(request.Database, changed.dump());
    rejects("duplicate database output rejected");
    changed = database;
    changed.at(0).erase("output");
    changed.at(0).at("arguments") = {compiler, "-c", "shared.cpp"};
    Write(request.Database, changed.dump());
    rejects("missing output rejected");
    changed = database;
    changed.at(0).at("output") = "Wrong.o";
    Write(request.Database, changed.dump());
    rejects("declared and actual output disagreement rejected");
    Write(request.Database, database.dump());
    Write(root / "nested/outer.rsp", "@missing.rsp");
    rejects("missing response file rejected");
    Write(root / "nested/outer.rsp", "@inner.rsp");
    Write(root / "inner.rsp", "@nested/outer.rsp");
    rejects("response cycle rejected");
    Write(root / "inner.rsp", std::string(std::size_t{1024} * 1024, 'x'));
    rejects("cumulative response bytes over 1 MiB rejected");
    for (int index = 0; index < 8; ++index)
    {
        Write(root / ("level" + std::to_string(index) + ".rsp"), "@level" + std::to_string(index + 1) + ".rsp");
    }
    Write(root / "level8.rsp", "-DEND=1");
    Write(root / "nested/outer.rsp", "@level0.rsp");
    rejects("response depth over eight rejected");
    // Seven nested level files plus the outer response are exactly eight.
    Write(root / "level6.rsp", "-DEND=1");
    FailureReport depthReport;
    expect(LoadCompileUnits(request, depthReport).has_value(), "eight response levels accepted");
    changed = database;
    changed.at(0).erase("arguments");
    changed.at(0).emplace("command", "'" + compiler + "' '-DJSON_USE_GLOBAL_UDLS=0' -c shared.cpp -o A.o");
    Write(request.Database, changed.dump());
    FailureReport commandReport;
    expect(LoadCompileUnits(request, commandReport).has_value(), "command representation supported using host quoting");
    Write(request.ObjectManifest, "a.o\nB.o\n");
    rejects("POSIX output identities remain case-sensitive");
    Write(request.ObjectManifest, "A.o\nB.o\n");
    changed = database;
    auto positional = first;
    positional.insert(positional.begin() + 3, {"-x", "c++"});
    positional.insert(positional.end(), {"-x", "c"});
    changed.at(0).at("arguments") = positional;
    Write(request.Database, changed.dump());
    positional.erase(positional.begin() + 5);
    positional.emplace_back("shared.cpp");
    FailureReport positionalReport;
    expect(!MatchCompilerInvocation(request, positional, root, positionalReport),
           "source position relative to language options remains significant");
    request.Compiler = root / "clang-cl";
    const auto clCompiler = request.Compiler.generic_string();
    const std::vector<std::string> clArguments{clCompiler, "-MD", "/c", "shared.cpp", "/FoA.o"};
    changed = nlohmann::json::array({
        {
            {"directory", root.generic_string()},
            {"file", "shared.cpp"},
            {"output", "A.o"},
            {"arguments", clArguments},
        },
    });
    Write(request.Database, changed.dump());
    Write(request.ObjectManifest, "A.o\n");
    FailureReport clReport;
    expect(!MatchCompilerInvocation(request, std::vector<std::string>{clCompiler, "/c", "shared.cpp", "/FoA.o"}, root,
                                    clReport),
           "clang-cl runtime -MD is not a dependency output option");
    failures += CheckRealDriver(root / "real", std::filesystem::absolute(args.back()));
    return failures == 0 ? 0 : 1;
}
