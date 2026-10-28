#include "Collection.h"
#include "CollectionRequest.h"
#include "CollectionState.h"
#include "Encoding.h"
#include "TechnicalClosure.h"
#include "ToolReport.h"

#include <Result.h>

#include <cstddef>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h> // IWYU pragma: keep - supplies prerequisites for native UTF-8 conversion.
#include <stringapiset.h>
#include <utility>
#include <winnls.h>
#endif

namespace
{
int RunMain(int argc, char* const* argv)
{
    pg::abi::FailureReport report;
    if (argc >= 2 &&
        std::string_view(std::span(argv, static_cast<std::size_t>(argc)).subspan<1>().front()) == "compile")
    {
        const auto arguments = std::span(argv, static_cast<std::size_t>(argc)).subspan<1>();
        int result = 2;
        if (arguments.size() >= 5 && std::string_view(arguments.subspan<1>().front()) == "--request" &&
            std::string_view(arguments.subspan<3>().front()) == "--")
        {
            const std::string requestText(arguments.subspan<2>().front());
            const auto request = pg::abi::ReadCollectionRequest(
                std::filesystem::path(std::u8string(requestText.begin(), requestText.end())), report,
                pg::abi::CollectionOutputWait::Waiting());
            if (request)
            {
                std::vector<std::string> invocation;
                for (const auto* argument : arguments.subspan<4>())
                {
                    invocation.emplace_back(argument);
                }
                result = pg::abi::RunCompilerInvocation(*request, invocation, report);
            }
        }
        else
        {
            (void)report.Add(pg::abi::ToolError::kArguments, "compile_arguments");
        }
        if (report.Count != 0)
        {
            report.WriteJson(std::cout);
        }
        return result;
    }
    if (argc == 4)
    {
        const auto command = std::span(argv, static_cast<std::size_t>(argc)).subspan<1>();
        const std::string_view operation(command.front());
        if (operation == "guard" || operation == "collect" || operation == "invalidate" ||
            operation == "guard-technical" || operation == "collect-technical")
        {
            int result = 2;
            if (std::string_view(command.subspan<1>().front()) == "--request")
            {
                const std::string_view text(command.back());
                const auto request = pg::abi::ReadCollectionRequest(
                    std::filesystem::path(std::u8string(text.begin(), text.end())), report);
                if (request)
                {
                    if (operation == "guard")
                    {
                        const auto state = pg::abi::GuardCollection(*request, report);
                        result = state ? 0 : 3;
                        if (state && *state == pg::abi::GuardDisposition::kRecollectRequired)
                        {
                            const auto collected = pg::abi::Collect(*request, report);
                            result = collected ? 0 : 3;
                            if (collected)
                            {
                                std::cout << "recollected\n";
                            }
                        }
                        else if (state)
                        {
                            std::cout << (*state == pg::abi::GuardDisposition::kCurrent ? "current\n"
                                                                                        : "build_required\n");
                        }
                    }
                    else
                    {
                        pg::Status status;
                        if (operation == "collect")
                        {
                            status = pg::abi::Collect(*request, report);
                        }
                        else if (operation == "guard-technical")
                        {
                            status = pg::abi::GuardTechnicalClosure(*request, report);
                        }
                        else if (operation == "collect-technical")
                        {
                            status = pg::abi::CollectTechnicalClosure(*request, report);
                        }
                        else
                        {
                            status = pg::abi::InvalidateCollection(*request, report);
                        }
                        result = status ? 0 : 3;
                    }
                }
            }
            else
            {
                (void)report.Add(pg::abi::ToolError::kArguments, "collection_arguments");
            }
            if (report.Count != 0)
            {
                report.WriteJson(std::cout);
            }
            return result;
        }
    }
    if (argc != 5)
    {
        (void)report.Add(pg::abi::ToolError::kArguments, "arguments");
        report.WriteJson(std::cout);
        std::cerr << "Usage: PanguAbiProfileTool <schema.json> <profile.json> <canonical-out> <sha256-out>\n";
        std::cerr << "       PanguAbiProfileTool compile --request <v2.json> -- <compiler argv...>\n";
        return 2;
    }
    const auto args = std::span(argv, static_cast<std::size_t>(argc)).subspan<1>();
    // Exactly four parameters, guarded by argc above.
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const int result = pg::abi::EncodeProfileFiles(args[0], args[1], args[2], args[3], report);
    // NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    if (result != 0)
    {
        report.WriteJson(std::cout);
        std::cerr << "ABI Profile operation failed; structured failure report is on stdout.\n";
    }
    return result;
}
} // namespace

#ifdef _WIN32
// The CRT entry point name and external linkage are fixed by Windows.
// NOLINTNEXTLINE(readability-identifier-naming,misc-use-internal-linkage)
int wmain(int argc, wchar_t* const* argv)
{
    std::vector<std::string> arguments;
    for (const auto* argument : std::span(argv, static_cast<std::size_t>(argc)))
    {
        const int size =
            ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argument, -1, nullptr, 0, nullptr, nullptr);
        if (size == 0)
        {
            return 2;
        }
        std::string text(static_cast<std::size_t>(size), '\0');
        (void)::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argument, -1, text.data(), size, nullptr, nullptr);
        text.pop_back();
        arguments.push_back(std::move(text));
    }
    std::vector<char*> pointers;
    pointers.reserve(arguments.size());
    for (auto& argument : arguments)
    {
        pointers.push_back(argument.data());
    }
    return RunMain(argc, pointers.data());
}
#else
int main(int argc, char* const* argv) { return RunMain(argc, argv); }
#endif
