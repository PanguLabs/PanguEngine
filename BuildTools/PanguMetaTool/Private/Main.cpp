#include "Driver/ClangInfo.h"
#include "Driver/GenerationRequest.h"
#include "Driver/RunGeneration.h"
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <span>
#include <string_view>

namespace
{
int Check()
{
    const auto result = pg::meta::tool::CheckClang();
    if (!result)
    {
        for (const auto& diagnostic : result.error())
        {
            std::cerr << diagnostic.Code << ": " << diagnostic.Message << '\n';
        }
        return 1;
    }
    return 0;
}

int Generate(const std::filesystem::path& path)
{
    if (!path.is_absolute())
    {
        std::cerr << "Usage: PanguMetaTool generate --request <absolute-json>\n";
        return 2;
    }
    const auto request = pg::meta::tool::ReadRequest(path);
    if (!request)
    {
        for (const auto& diagnostic : request.error())
        {
            std::cerr << diagnostic.Source.File << ": " << diagnostic.Code << ": " << diagnostic.Message << '\n';
        }
        return 1;
    }
    const auto result = pg::meta::tool::detail::RunGenerationFromFile(*request, path);
    if (!result)
    {
        for (const auto& diagnostic : result.error())
        {
            std::cerr << diagnostic.Source.File << ':' << diagnostic.Source.Line << ':' << diagnostic.Source.Column
                      << ": " << diagnostic.Code << ": " << diagnostic.Message << '\n';
        }
        return 1;
    }
    return 0;
}
} // namespace

#ifdef _WIN32
// Windows CRT 要求固定入口名和外部链接。
// NOLINTNEXTLINE(misc-use-internal-linkage, readability-identifier-naming)
int wmain(int argc, wchar_t** argv)
{
    const std::span<wchar_t*> args{argv, static_cast<std::size_t>(argc)};
    if (args.size() == 4 && std::wstring_view(*std::next(args.begin())) == L"generate" &&
        std::wstring_view(*std::next(args.begin(), 2)) == L"--request")
    {
        return Generate(std::filesystem::path(args.back()));
    }
    if (args.size() == 2 && std::wstring_view(args.back()) == L"--check-clang")
#else
int main(int argc, char* argv[])
{
    const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
    if (args.size() == 4 && std::string_view(*std::next(args.begin())) == "generate" &&
        std::string_view(*std::next(args.begin(), 2)) == "--request")
    {
        return Generate(std::filesystem::path(args.back()));
    }
    if (args.size() == 2 && std::string_view(args.back()) == "--check-clang")
#endif
    {
        return Check();
    }
    std::cerr << "Usage: PanguMetaTool --check-clang | generate --request <absolute-json>\n";
    return 2;
}
