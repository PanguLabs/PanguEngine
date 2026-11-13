#include "FileStoreAllocationCases.h"

#include <LogConfig.h>
#include <LogFileStore.h>
#include <LogReports.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace
{
const std::filesystem::path& Directory()
{
    static const std::filesystem::path kDirectory(PG_LOG_ALLOCATION_ROOT);
    return kDirectory;
}
} // namespace
bool PrepareFileStoreAllocationCases()
{
    std::error_code error;
    std::filesystem::create_directories(Directory(), error);
    return !error;
}
bool RunFileStoreAllocationCases() noexcept
{
    pg::log::LogConfig config;
    config.Directory = Directory().native();
    config.PartitionId.front() = std::byte{1};
    config.FileBytes = 13312;
    config.PartitionBytes = 26624;
    std::array<char, 12288> line{};
    line.fill('a');
    for (int session = 0; session < 2; ++session)
    {
        pg::log::LogInitReport report;
        pg::log::detail::FileStore store;
        if (!store.Open(config, report))
        {
            return false;
        }
        for (int index = 0; index < 5; ++index)
        {
            if (!store.Write(std::string_view(line.data(), line.size())).Complete)
            {
                return false;
            }
        }
        if (!store.Flush().Complete || !store.Close() || store.Inspect().CleanupPending)
        {
            return false;
        }
    }
    return true;
}
