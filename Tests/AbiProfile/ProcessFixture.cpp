#include "../../BuildTools/AbiProfile/Private/Process.h"
#include "../../BuildTools/AbiProfile/Private/ToolReport.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stop_token>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h> // IWYU pragma: keep - SDK prerequisites.
#include <corecrt_io.h>
#include <fcntl.h>
#include <minwindef.h>
#include <processenv.h>
#include <processthreadsapi.h>
#include <stdio.h> // NOLINT(modernize-deprecated-headers) - declares Windows _fileno.
#include <stringapiset.h>
#include <utility>
#include <winnls.h>
#else
#include <csignal>
#include <signal.h> // NOLINT(modernize-deprecated-headers) - native SIGPIPE disposition.
#include <unistd.h>
#ifdef __APPLE__
#include <sys/signal.h>
#endif
#endif

namespace
{
int Run(std::span<const std::string> args)
{
    if (args.empty())
    {
        return 2;
    }
    if (args.front() == "write-marker" && args.size() == 2)
    {
#ifndef _WIN32
        // Keep this producer alive after an orphaned wrapper closes its pipe,
        // so the delayed marker detects missing cleanup instead of SIGPIPE.
        const bool pipeDefault = std::signal(SIGPIPE, SIG_IGN) == SIG_DFL;
#endif
        const auto marker = std::filesystem::path(std::u8string(args.back().begin(), args.back().end()));
        {
            std::ofstream started(marker.string() + ".started");
#ifdef _WIN32
            started << ::GetCurrentProcessId();
#else
            started << ::getpid() << ' ' << (pipeDefault ? "default" : "changed");
#endif
        }
        const std::jthread delayed(
            [&marker]
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(700));
                std::ofstream file(marker);
                file << "producer survived";
            });
        const std::string block(4096, 'x');
        for (int i = 0; i < 16384; ++i)
        {
            std::cout << block;
        }
        return 0;
    }
    if (args.front() == "forward" && args.size() == 4)
    {
        if (args.back() == "cancel-delayed")
        {
            // Model executable loading/scheduling before the operation begins.
            std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        }
#ifndef _WIN32
        // The spawned producer must retain this default disposition. Only the
        // private relay is allowed to ignore SIGPIPE.
        (void)std::signal(SIGPIPE, SIG_DFL);
#endif
        pg::abi::ProcessRequest request;
        request.Program =
            std::filesystem::path(std::u8string(args.subspan(1).front().begin(), args.subspan(1).front().end()));
        request.Arguments = {"write-marker", args.subspan(2).front()};
        request.OutputMode = pg::abi::ProcessOutputMode::kForward;
        request.Timeout = args.back() == "timeout" ? std::chrono::milliseconds(150) : std::chrono::milliseconds(0);
        std::stop_source cancellation;
        request.Cancellation = cancellation.get_token();
        std::atomic<std::int64_t> cancelledAt{};
        const auto markerPath =
            std::filesystem::path(std::u8string(args.subspan(2).front().begin(), args.subspan(2).front().end()));
        const std::jthread canceller(
            [&cancellation, &args, &cancelledAt, &markerPath]
            {
                if (args.back().starts_with("cancel"))
                {
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                    while (!std::filesystem::exists(markerPath.string() + ".started") &&
                           std::chrono::steady_clock::now() < deadline)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    cancelledAt = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count();
                    (void)cancellation.request_stop();
                }
            });
        pg::abi::FailureReport report;
        const auto processStarted = std::chrono::steady_clock::now();
        const auto result = pg::abi::RunProcess(request, report);
        const auto finishedAt = std::chrono::steady_clock::now();
        const auto elapsed =
            cancelledAt.load() == 0
                ? std::chrono::duration_cast<std::chrono::milliseconds>(finishedAt - processStarted).count()
                : std::chrono::duration_cast<std::chrono::milliseconds>(finishedAt.time_since_epoch()).count() -
                      cancelledAt.load();
        const auto resultPath =
            std::filesystem::path(std::u8string(args.subspan(2).front().begin(), args.subspan(2).front().end()));
        std::ofstream file(resultPath.string() + ".result");
        file << (!result && report.Count != 0 ? report.Entries.front().Stage.View() : "unexpected success") << ' '
             << elapsed;
        return !result ? 23 : 24;
    }
    if (args.front() == "compile-fixture" || args.front() == "mutate-fixture" ||
        args.front() == "mutate-header-fixture" || args.front() == "cancel-fixture" || args.front() == "pause-fixture")
    {
        std::filesystem::path output;
        std::filesystem::path source;
        for (std::size_t index = 1; index + 1 < args.size(); ++index)
        {
            if (args.subspan(index).front() == "-o")
            {
                output = args.subspan(index + 1).front();
            }
            if (args.subspan(index).front() == "-c")
            {
                source = args.subspan(index + 1).front();
            }
        }
        if (output.empty() || source.empty())
        {
            return 2;
        }
        {
            std::ofstream marker(output.string() + ".invoked", std::ios::app);
            marker << "invoked\n";
        }
        if (args.front() == "pause-fixture")
        {
            const auto resume = output.string() + ".resume";
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            std::error_code error;
            while (!std::filesystem::exists(resume, error))
            {
                if (error || std::chrono::steady_clock::now() >= deadline)
                {
                    return 3;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        if (args.front() == "cancel-fixture")
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
        }
        if (args.front() == "mutate-header-fixture")
        {
            std::ofstream changed(source.parent_path() / "unit.h");
            changed << "// changed known header\n";
        }
        if (args.front() == "mutate-fixture")
        {
            std::ofstream changed(source);
            changed << "// modified while compiler running\n";
        }
        {
            std::ofstream file(output, std::ios::binary);
            file << "real child output bytes";
        }
        return 0;
    }
    if (args.front() == "exit")
    {
        std::cerr << "compiler diagnostic\n";
        return 17;
    }
    if (args.front() == "failure-flood")
    {
        std::cout << std::string(16384, 'o');
        std::cerr << std::string(65536, 'e');
        return 17;
    }
    if (args.front() == "echo")
    {
        for (const auto& arg : args.subspan(1))
        {
            std::cout << arg.size() << ':' << arg << '\n';
        }
        return 0;
    }
    if (args.front() == "flood")
    {
        const std::string block(4096, 'x');
        std::jthread const errorWriter(
            [&block]
            {
                for (int i = 0; i < 256; ++i)
                {
                    std::cerr << block;
                }
            });
        for (int i = 0; i < 256; ++i)
        {
            std::cout << block;
        }
        return 0;
    }
    if (args.front() == "sleep" && args.size() == 2)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        const auto& text = args.back();
        std::ofstream marker(std::filesystem::path(std::u8string(text.begin(), text.end())));
        marker << "child survived";
        return 0;
    }
    if (args.front() == "context")
    {
        const auto current = std::filesystem::current_path().generic_u8string();
        std::cout << std::string(current.begin(), current.end()) << '\n';
#ifdef _WIN32
        const DWORD size = ::GetEnvironmentVariableW(L"PANGU_PROCESS_TEST", nullptr, 0);
        std::wstring value(size, L'\0');
        (void)::GetEnvironmentVariableW(L"PANGU_PROCESS_TEST", value.data(), size);
        const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string encoded(static_cast<std::size_t>(bytes), '\0');
        (void)::WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, encoded.data(), bytes, nullptr, nullptr);
        encoded.pop_back();
        std::cout << encoded;
#else
        // Fixture has joined all writers and never mutates its environment.
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        const char* value = std::getenv("PANGU_PROCESS_TEST");
        std::cout << (value != nullptr ? value : "missing");
#endif
        return 0;
    }
    return 2;
}
} // namespace

#ifdef _WIN32
// Windows CRT fixes this entry point's spelling and external linkage.
// NOLINTNEXTLINE(readability-identifier-naming,misc-use-internal-linkage)
int wmain(int argc, wchar_t* const* argv)
{
    (void)::_setmode(::_fileno(stdout), _O_BINARY);
    (void)::_setmode(::_fileno(stderr), _O_BINARY);
    std::vector<std::string> args;
    for (const auto* arg : std::span(argv, static_cast<std::size_t>(argc)).subspan(1))
    {
        const int size = ::WideCharToMultiByte(CP_UTF8, 0, arg, -1, nullptr, 0, nullptr, nullptr);
        std::string value(static_cast<std::size_t>(size), '\0');
        (void)::WideCharToMultiByte(CP_UTF8, 0, arg, -1, value.data(), size, nullptr, nullptr);
        value.pop_back();
        args.push_back(std::move(value));
    }
    return Run(args);
}
#else
int main(int argc, char* const* argv)
{
    std::vector<std::string> args;
    for (const auto* arg : std::span(argv, static_cast<std::size_t>(argc)).subspan(1))
    {
        args.emplace_back(arg);
    }
    return Run(args);
}
#endif
