#include "Driver/CompilationContext.h"
#include "Driver/Diagnostic.h"
#include "Driver/GenerationRequest.h"
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticIDs.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/TargetOptions.h>
#include <clang/Driver/Compilation.h>
#include <clang/Driver/Driver.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Options/Options.h>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Option/Arg.h>
#include <llvm/Option/ArgList.h>
#include <llvm/Option/OptTable.h>
#include <llvm/Option/Option.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/ErrorOr.h>
#include <llvm/Support/VirtualFileSystem.h>
#include <llvm/TargetParser/Host.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace pg::meta::tool
{
namespace
{
using Path = std::filesystem::path;
using Json = nlohmann::json;
std::string Utf8(const Path& path)
{
    const auto value = path.generic_u8string();
    return {value.begin(), value.end()};
}
Path Native(std::string_view text) { return {std::u8string(text.begin(), text.end())}; }
std::unexpected<Diagnostics> Invalid(std::string message, const Path& path = {})
{
    return std::unexpected(
        Diagnostics{{.Code = "invalid_context", .Message = std::move(message), .Source = {.File = Utf8(path)}}});
}
bool Same(const Path& left, const Path& right)
{
    std::error_code error;
    if (std::filesystem::equivalent(left, right, error) && !error)
    {
        return true;
    }
    error.clear();
    const auto a = std::filesystem::weakly_canonical(left, error);
    if (error)
    {
        return false;
    }
    const auto b = std::filesystem::weakly_canonical(right, error);
    return !error && a == b;
}
class DependencyFileSystem final : public llvm::vfs::ProxyFileSystem
{
public:
    explicit DependencyFileSystem(std::vector<Path>& dependencies)
        : ProxyFileSystem(llvm::vfs::getRealFileSystem())
        , Dependencies(dependencies)
    {
    }
    llvm::ErrorOr<std::unique_ptr<llvm::vfs::File>> openFileForRead(const llvm::Twine& path) override
    {
        auto result = ProxyFileSystem::openFileForRead(path);
        if (result)
        {
            std::error_code error;
            const auto canonical = std::filesystem::canonical(Native(path.str()), error);
            if (!error)
            {
                Dependencies.push_back(canonical);
            }
        }
        return result;
    }

private:
    // 回调状态在本次同步扫描期间借用，禁止超出调用寿命。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    std::vector<Path>& Dependencies;
};
ToolResult<std::vector<std::string>> Expand(const Json& entry, const Path& directory, std::vector<Path>& dependencies)
{
    llvm::BumpPtrAllocator allocator;
    llvm::StringSaver saver(allocator);
    llvm::SmallVector<const char*, 32> arguments;
    const auto validText = [](const Json& text)
    { return text.is_string() && !text.get_ref<const std::string&>().contains('\0'); };
    if (entry.contains("arguments"))
    {
        if (!entry.at("arguments").is_array())
        {
            return Invalid("arguments 必须是字符串数组");
        }
        for (const auto& argument : entry.at("arguments"))
        {
            if (!validText(argument))
            {
                return Invalid("arguments 含无效字符串");
            }
            // LLVM StringSaver::save 保证额外保存结尾 NUL，StringRef 的 size 不包含该字节。
            // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
            arguments.push_back(saver.save(argument.get_ref<const std::string&>()).data());
        }
    }
    else
    {
        if (!entry.contains("command") || !validText(entry.at("command")))
        {
            return Invalid("缺少 command/arguments");
        }
#ifdef _WIN32
        llvm::cl::TokenizeWindowsCommandLineFull(entry.at("command").get_ref<const std::string&>(), saver, arguments);
#else
        llvm::cl::TokenizeGNUCommandLine(entry.at("command").get_ref<const std::string&>(), saver, arguments);
#endif
    }
    if (arguments.empty())
    {
        return Invalid("编译命令为空");
    }
#ifdef _WIN32
    constexpr auto kTokenizer = llvm::cl::TokenizeWindowsCommandLine;
#else
    constexpr auto kTokenizer = llvm::cl::TokenizeGNUCommandLine;
#endif
    const auto working = Utf8(directory);
    llvm::cl::ExpansionContext expansion(allocator, kTokenizer);
    const auto filesystem = llvm::makeIntrusiveRefCnt<DependencyFileSystem>(dependencies);
    expansion.setVFS(filesystem.get());
    // 与 Clang driver 相同，嵌套 rsp 的相对路径基于编译工作目录。
    expansion.setCurrentDir(working);
    if (auto error = expansion.expandResponseFiles(arguments))
    {
        return Invalid(llvm::toString(std::move(error)));
    }
    std::vector<std::string> result;
    for (const auto* argument : arguments)
    {
        if (std::string_view(argument).starts_with('@'))
        {
            return Invalid("响应文件不存在或无法展开", Native(std::string_view(argument).substr(1)));
        }
        result.emplace_back(argument);
    }
    return result;
}
ToolResult<void> ValidateConfiguration(const GenerationRequest& request)
{
    std::ifstream stream(request.Context.Database.parent_path() / "CMakeCache.txt");
    if (!stream)
    {
        return Invalid("缺少数据库对应的 CMakeCache.txt", request.Context.Database);
    }
    std::string line;
    std::string config;
    std::string generator;
    constexpr std::string_view kConfig = "CMAKE_BUILD_TYPE:STRING=";
    constexpr std::string_view kGenerator = "CMAKE_GENERATOR:INTERNAL=";
    while (std::getline(stream, line))
    {
        if (line.ends_with('\r'))
        {
            line.pop_back();
        }
        if (line.starts_with(kConfig))
        {
            config = line.substr(kConfig.size());
        }
        if (line.starts_with(kGenerator))
        {
            generator = line.substr(kGenerator.size());
        }
        if (line.starts_with("CMAKE_CONFIGURATION_TYPES:") && !line.ends_with('='))
        {
            return Invalid("不支持多配置数据库");
        }
    }
    if (config != request.Config || generator != "Ninja")
    {
        return Invalid("数据库配置或生成器与请求不匹配", request.Context.Database);
    }
    return {};
}
class DriverDiagnostics final : public clang::DiagnosticConsumer
{
public:
    Diagnostics Errors;
    void HandleDiagnostic(clang::DiagnosticsEngine::Level level, const clang::Diagnostic& info) override
    {
        clang::DiagnosticConsumer::HandleDiagnostic(level, info);
        if (level < clang::DiagnosticsEngine::Error)
        {
            return;
        }
        llvm::SmallString<256> text;
        info.FormatDiagnostic(text);
        Errors.push_back({.Code = "invalid_context", .Message = std::string(text)});
    }
};
ToolResult<void> ValidateDriver(const std::vector<std::string>& arguments)
{
    DriverDiagnostics consumer;
    clang::DiagnosticOptions options;
    auto const ids = llvm::makeIntrusiveRefCnt<clang::DiagnosticIDs>();
    clang::DiagnosticsEngine diagnostics(ids, options, &consumer, false);
    clang::driver::Driver driver(PANGU_META_HOST_COMPILER_PATH, llvm::sys::getDefaultTargetTriple(), diagnostics,
                                 "PanguMetaTool context", llvm::vfs::getRealFileSystem());
    llvm::SmallVector<const char*, 32> args{PANGU_META_HOST_COMPILER_PATH};
    for (const auto& argument : arguments)
    {
        args.push_back(argument.c_str());
    }
    const std::unique_ptr<clang::driver::Compilation> compilation(driver.BuildCompilation(args));
    if (!consumer.Errors.empty())
    {
        return std::unexpected(std::move(consumer.Errors));
    }
    if (!compilation || compilation->getJobs().size() != 1)
    {
        return Invalid("上下文必须产生单一 Clang 前端任务");
    }
    const auto& frontend = compilation->getJobs().begin()->getArguments();
    if (frontend.empty() || std::string_view(frontend.front()) != "-cc1")
    {
        return Invalid("上下文不是 Clang C++ 前端任务");
    }
    clang::CompilerInvocation invocation;
    const bool parsed = clang::CompilerInvocation::CreateFromArgs(invocation, llvm::ArrayRef(frontend).drop_front(),
                                                                  diagnostics, PANGU_META_HOST_COMPILER_PATH);
    if (!consumer.Errors.empty())
    {
        return std::unexpected(std::move(consumer.Errors));
    }
    if (!parsed || llvm::Triple(invocation.getTargetOpts().Triple).getArch() == llvm::Triple::UnknownArch)
    {
        return Invalid("无效前端参数或目标架构");
    }
    return {};
}
ToolResult<std::vector<std::string>> Sanitize(const std::vector<std::string>& command, const Path& directory,
                                              const GenerationRequest& request, const HeaderInput& header)
{
    const auto program = Native(command.front()).stem().string();
    if (program != "clang" && program != "clang++")
    {
        return Invalid("仅支持 Clang GNU driver 模式");
    }
    llvm::SmallVector<const char*, 32> raw;
    for (std::size_t i = 1; i < command.size(); ++i)
    {
        raw.push_back(command.at(i).c_str());
    }
    unsigned missingIndex = 0;
    unsigned missingCount = 0;
    // GNU 模式不能识别 CL 的 /U 等选项，否则 /Users 下的绝对路径会被吞作宏参数。
    const llvm::opt::InputArgList parsed = clang::getDriverOptTable().ParseArgs(
        raw, missingIndex, missingCount, llvm::opt::Visibility(clang::options::ClangOption));
    if (missingCount != 0)
    {
        return Invalid("编译参数缺少值");
    }
    std::vector<std::string> result;
    unsigned sourceCount = 0;
    for (const auto* arg : parsed)
    {
        const auto& option = arg->getOption();
        const std::string spelling = arg->getSpelling().str();
        const auto complete = arg->getAsString(parsed);
        if (option.matches(clang::options::OPT_UNKNOWN))
        {
            return Invalid("未知编译参数: " + spelling);
        }
        if (option.matches(clang::options::OPT_INPUT))
        {
            if (!Same(directory / Native(arg->getValue()), request.Context.Source))
            {
                return Invalid("编译命令包含其他输入");
            }
            ++sourceCount;
            continue;
        }
        if (spelling == "-c" || spelling == "-o" || spelling == "-M" || spelling == "-MM" || spelling == "-MD" ||
            spelling == "-MMD" || spelling == "-MF" || spelling == "-MT" || spelling == "-MQ" || spelling == "-MP" ||
            spelling == "-MG" || spelling == "-MJ" || spelling == "-dependency-file" ||
            spelling == "-serialize-diagnostics" || spelling.starts_with("-resource-dir"))
        {
            continue;
        }
        if (complete == "-w" || complete == "-Wno-everything" || complete == "-Wno-error" ||
            complete == "-Wno-attributes" || complete == "-Wno-unknown-attributes" ||
            complete == "-Wno-ignored-attributes" || complete.starts_with("-Wno-error="))
        {
            continue;
        }
        if (spelling == "-Xclang")
        {
            const std::string_view value = arg->getValue();
            // CMake 的 Windows Debug 命令使用 cc1 CodeView 开关；纯语法扫描无需调试信息。
            if (value == "-gcodeview")
            {
                continue;
            }
            if (!value.starts_with("--dependent-lib="))
            {
                return Invalid("不支持不透明 -Xclang 参数: " + std::string(value));
            }
        }
        if (spelling.starts_with("--driver-mode") || spelling.starts_with("--config") || spelling == "-cc1" ||
            spelling.starts_with("-fmodule") || spelling.starts_with("-fprebuilt-module") || spelling.contains("pch") ||
            spelling == "-E" || spelling == "-S" || spelling == "-emit-llvm" || spelling == "-Xpreprocessor" ||
            spelling.starts_with("-Wp,") || spelling.starts_with("-Xarch") || spelling == "-working-directory" ||
            spelling.starts_with("-fplugin") || spelling.starts_with("-fdiagnostics-suppress") ||
            spelling == "-ivfsoverlay" || spelling.starts_with("--warning-suppression-mappings"))
        {
            return Invalid("不支持的上下文模式: " + spelling);
        }
        if (spelling == "-include")
        {
            const auto original = directory / Native(arg->getValue());
            std::error_code error;
            if (!std::filesystem::is_regular_file(original, error) || original.extension() == ".pch" ||
                original.extension() == ".gch")
            {
                return Invalid("强制包含必须可还原为原始头", original);
            }
            // 原始头直接交给 cc1，禁止 driver 自动替换为旁侧 .pch/.gch。
            result.emplace_back("-Xclang");
            result.emplace_back("-include");
            result.emplace_back("-Xclang");
            result.push_back(Utf8(original));
            continue;
        }
        if (spelling == "-x" && std::string_view(arg->getValue()) != "c++")
        {
            return Invalid("仅支持原始 C++ 上下文");
        }
        llvm::opt::ArgStringList rendered;
        arg->render(parsed, rendered);
        for (const auto* value : rendered)
        {
            result.emplace_back(value);
        }
    }
    if (sourceCount != 1)
    {
        return Invalid("命令必须恰好包含 context.source 一次");
    }
    result.insert(result.end(), {
                                    "-working-directory",
                                    Utf8(directory),
                                    "-resource-dir",
                                    Utf8(request.Context.ResourceDir),
                                    "-x",
                                    "c++-header",
                                    "-fsyntax-only",
                                    "-Wunknown-attributes",
                                    "-Werror=unknown-attributes",
                                    Utf8(header.Path),
                                });
    const auto valid = ValidateDriver(result);
    if (!valid)
    {
        return std::unexpected(valid.error());
    }
    return result;
}
} // namespace

ToolResult<CompilationInvocation> BuildCompilationInvocation(const GenerationRequest& request,
                                                             const HeaderInput& header)
{
    std::vector<Path> dependencies;
    const auto configuration = ValidateConfiguration(request);
    if (!configuration)
    {
        return std::unexpected(configuration.error());
    }
    if (!request.Context.Source.is_absolute() || !header.Path.is_absolute() ||
        !Same(request.Context.ResourceDir, Path(PANGU_META_RESOURCE_PATH)))
    {
        return Invalid("上下文必须使用绝对输入与配套 SDK 资源根");
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(request.Context.ResourceDir / "include" / "stddef.h", error))
    {
        return Invalid("SDK 资源头缺失");
    }
    std::ifstream stream(request.Context.Database, std::ios::binary);
    if (!stream)
    {
        return Invalid("无法读取 compilation database", request.Context.Database);
    }
    const auto database = Json::parse(stream, nullptr, false);
    if (!database.is_array())
    {
        return Invalid("compilation database 必须是 JSON 数组", request.Context.Database);
    }
    std::vector<std::string> selected;
    Path selectedDirectory;
    unsigned matches = 0;
    for (const auto& entry : database)
    {
        if (!entry.is_object() || !entry.contains("directory") || !entry.at("directory").is_string() ||
            !entry.contains("file") || !entry.at("file").is_string())
        {
            return Invalid("数据库项缺少 directory/file");
        }
        const auto directory = Native(entry.at("directory").get_ref<const std::string&>());
        if (!directory.is_absolute())
        {
            return Invalid("数据库 directory 必须绝对");
        }
        if (!Same(directory / Native(entry.at("file").get_ref<const std::string&>()), request.Context.Source))
        {
            continue;
        }
        const auto command = Expand(entry, directory, dependencies);
        if (!command)
        {
            return std::unexpected(command.error());
        }
        Path object;
        llvm::SmallVector<const char*, 32> raw;
        for (std::size_t i = 1; i < command->size(); ++i)
        {
            raw.push_back(command->at(i).c_str());
        }
        unsigned missingIndex = 0;
        unsigned missingCount = 0;
        const llvm::opt::InputArgList parsed = clang::getDriverOptTable().ParseArgs(
            raw, missingIndex, missingCount, llvm::opt::Visibility(clang::options::ClangOption));
        if (missingCount != 0)
        {
            return Invalid("编译参数缺少值");
        }
        if (const auto* output = parsed.getLastArg(clang::options::OPT_o))
        {
            object = directory / Native(output->getValue());
        }
        if (entry.contains("output"))
        {
            if (!entry.at("output").is_string())
            {
                return Invalid("output 必须是路径字符串");
            }
            const auto advertised = directory / Native(entry.at("output").get_ref<const std::string&>());
            if (!object.empty() && !Same(object, advertised))
            {
                return Invalid("数据库 output 与命令 -o 不一致");
            }
            object = advertised;
        }
        if (!request.Context.Object.empty() && !Same(object, request.Context.Object))
        {
            continue;
        }
        selected = *command;
        selectedDirectory = directory;
        ++matches;
    }
    if (matches != 1)
    {
        return Invalid("context.source/object 必须精确匹配唯一命令", request.Context.Database);
    }
    if (request.Context.Object.empty() &&
        !Same(request.Context.Source, request.OutputRoot / "Private" / "MetaContext.cpp"))
    {
        return Invalid("空 object 仅支持本目标/配置的专属 MetaContext.cpp");
    }
    auto arguments = Sanitize(selected, selectedDirectory, request, header);
    if (!arguments)
    {
        return std::unexpected(arguments.error());
    }
    for (const auto& path : {request.Context.Database, request.Context.Database.parent_path() / "CMakeCache.txt"})
    {
        const auto canonical = std::filesystem::canonical(path, error);
        if (error)
        {
            return Invalid("无法规范化上下文依赖", path);
        }
        dependencies.push_back(canonical);
    }
    return CompilationInvocation{.Arguments = std::move(*arguments), .Dependencies = std::move(dependencies)};
}
ToolResult<std::vector<std::string>> BuildInvocation(const GenerationRequest& request, const HeaderInput& header)
{
    auto result = BuildCompilationInvocation(request, header);
    if (!result)
    {
        return std::unexpected(result.error());
    }
    return std::move(result->Arguments);
}
} // namespace pg::meta::tool
