#pragma once

#include "ToolReport.h"
#include <Result.h>

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stop_token>
#include <string>
#include <vector>

namespace pg::abi
{
enum class ProcessOutputMode : std::uint8_t
{
    kCapture,
    kForward,
};

struct ProcessRequest
{
    std::filesystem::path Program;
    // Arguments excludes argv[0]; Program is used verbatim as argv[0]. UTF-8, no shell.
    std::vector<std::string> Arguments;
    std::filesystem::path Directory;
    // Overrides inherited environment entries; empty values are preserved.
    std::map<std::string, std::string> Environment;
    ProcessOutputMode OutputMode{ProcessOutputMode::kCapture};
    // Zero disables the timeout (required for actual compilation).
    std::chrono::milliseconds Timeout{std::chrono::seconds(120)};
    std::size_t CaptureLimitBytes{std::size_t{16} * 1024 * 1024};
    std::stop_token Cancellation;
    // Optional signal-handler flag, borrowed only for this synchronous call.
    const volatile std::sig_atomic_t* InterruptFlag{};
};

struct ProcessResult
{
    int ExitCode{};
    std::string Stdout;
    std::string Stderr;
};

[[nodiscard]] Result<ProcessResult> RunProcess(const ProcessRequest& request, FailureReport& report);
} // namespace pg::abi
