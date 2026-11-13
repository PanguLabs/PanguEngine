#include "LogConfig.h"
#include "LogReports.h"
#include <LogFileStore.h>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

namespace
{
bool WaitFor(const std::filesystem::path& path)
{
    for (int attempt = 0; attempt < 500; ++attempt)
    {
        std::error_code error;
        if (std::filesystem::exists(path, error))
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
} // namespace

int main(int argc, char* const* argv)
{
    if (argc != 3)
    {
        return 10;
    }
    // argc 已验证为 3，argv 是 main 的 C ABI。
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const auto directory = std::filesystem::absolute(argv[2]);
    pg::log::LogConfig config;
    config.Directory = directory.native();
    config.PartitionId.front() = std::byte{1};
    config.FileBytes = 13312;
    config.PartitionBytes = 13312;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) 同上。
    const std::string_view mode(argv[1]);
    if (mode == "contender" && !WaitFor(directory / "ready"))
    {
        return 20;
    }
    pg::log::LogInitReport report;
    pg::log::detail::FileStore store;
    const auto status = store.Open(config, report);
    if (mode == "contender")
    {
        std::ofstream signal(directory / "release");
        signal << "release";
        return !status && status.error().Code() == 6 ? 0 : 21;
    }
    if (!status)
    {
        return status.error().Code() == 6 ? 6 : 11;
    }
    if (mode == "holder")
    {
        {
            std::ofstream signal(directory / "ready");
            signal << "ready";
        }
        return WaitFor(directory / "release") ? 0 : 22;
    }
    if (mode == "crash")
    {
        if (!store.Write(std::string(12288, 'c')).Complete)
        {
            return 23;
        }
        std::_Exit(42);
    }
    if (mode == "recover" && store.Inspect().ReclaimedBytes != 13312)
    {
        return 24;
    }
    return store.Write("fixture\n").Complete ? 0 : 12;
}
