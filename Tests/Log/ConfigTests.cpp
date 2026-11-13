#include <LogConfig.h>
#include <LogRecord.h>
#include <LogTypes.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <string>

namespace
{
constexpr pg::log::LogMemoryLayout kLayout{.QueueSlotBytes = sizeof(pg::log::LogRecord), .FixedBytes = 32768};
pg::log::LogConfig ConsoleConfig()
{
    pg::log::LogConfig config;
    config.Enabled = pg::log::OutputMask::kConsole;
    return config;
}
} // namespace

TEST(LogConfig, RequiredMustBeEnabled)
{
    auto config = ConsoleConfig();
    config.Required = pg::log::OutputMask::kFile;
    const auto result = pg::log::ValidateLogConfig(config, kLayout);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), pg::log::MakeLogError(pg::log::LogError::kInvalidConfig));
    config.Required = pg::log::OutputMask::kConsole;
    EXPECT_TRUE(pg::log::ValidateLogConfig(config, kLayout));
}

TEST(LogConfig, OptionalFileInputDoesNotRejectConsoleConfiguration)
{
    // 目录缺失只阻止 File；实例级校验不能因此把健康 Console 一同拒绝。
    const pg::log::LogConfig config;
    EXPECT_TRUE(pg::log::ValidateLogConfig(config, kLayout));
    EXPECT_FALSE(pg::log::ValidateLogFileConfig(config));
}

TEST(LogConfig, CapacityArithmeticOverflow)
{
    auto config = ConsoleConfig();
    constexpr auto kMax = std::numeric_limits<std::size_t>::max();
    EXPECT_FALSE(pg::log::ValidateLogConfig(config, {.QueueSlotBytes = kMax, .FixedBytes = 32768}));
    EXPECT_FALSE(
        pg::log::ValidateLogConfig(config, {.QueueSlotBytes = sizeof(pg::log::LogRecord), .FixedBytes = kMax}));
    config.QueueCapacity = kMax;
    EXPECT_FALSE(pg::log::ValidateLogConfig(config, kLayout));
}

TEST(LogConfig, MemoryBudgetIncludesSlotsFixedResourcesAndStack)
{
    auto config = ConsoleConfig();
    const auto bytes = pg::log::RequiredLogMemory(config, kLayout);
    ASSERT_TRUE(bytes);
    config.MemoryBudgetBytes = *bytes - 1;
    EXPECT_FALSE(pg::log::ValidateLogConfig(config, kLayout));
    ++config.MemoryBudgetBytes;
    EXPECT_TRUE(pg::log::ValidateLogConfig(config, kLayout));
    config.PriorityReserve = config.QueueCapacity;
    EXPECT_FALSE(pg::log::ValidateLogConfig(config, kLayout));
}

TEST(LogConfig, FileInputIsBoundedAndDisabledFileNeedsNoPath)
{
    auto config = ConsoleConfig();
    EXPECT_TRUE(pg::log::ValidateLogFileConfig(config));
    config.Enabled = pg::log::OutputMask::kFile;
    EXPECT_FALSE(pg::log::ValidateLogFileConfig(config));
    std::basic_string<std::filesystem::path::value_type> path(1024,
                                                              static_cast<std::filesystem::path::value_type>('a'));
#ifdef _WIN32
    path.replace(0, 3, L"C:\\");
#else
    path.front() = '/';
#endif
    config.Directory = path;
    config.PartitionId.front() = std::byte{1};
    EXPECT_TRUE(pg::log::ValidateLogFileConfig(config));
    path.push_back(static_cast<std::filesystem::path::value_type>('a'));
    config.Directory = path;
    EXPECT_FALSE(pg::log::ValidateLogFileConfig(config));
    path.resize(1024);
    path.at(8) = {};
    config.Directory = path;
    EXPECT_FALSE(pg::log::ValidateLogFileConfig(config));
#ifdef _WIN32
    config.Directory = L"C:\\logs\\partition";
#else
    config.Directory = "/logs/partition";
#endif
    EXPECT_TRUE(pg::log::ValidateLogFileConfig(config));
    config.FileBytes = 100;
    EXPECT_FALSE(pg::log::ValidateLogFileConfig(config));
    config.FileBytes = 4ULL * 1024 * 1024;
    config.PartitionBytes = config.FileBytes - 1;
    EXPECT_FALSE(pg::log::ValidateLogFileConfig(config));
}

TEST(LogConfig, RejectsUnknownMasksAndInvalidGeometry)
{
    auto config = ConsoleConfig();
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) 故意注入外部无效位。
    config.Enabled = static_cast<pg::log::OutputMask>(8);
    EXPECT_FALSE(pg::log::ValidateLogConfig(config, kLayout));
    config = ConsoleConfig();
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) 故意注入外部无效等级。
    config.Minimum = static_cast<pg::log::Level>(255);
    EXPECT_FALSE(pg::log::ValidateLogConfig(config, kLayout));
    config = ConsoleConfig();
    config.DefaultWait = std::chrono::milliseconds{-1};
    EXPECT_FALSE(pg::log::ValidateLogConfig(config, kLayout));
    config = ConsoleConfig();
    EXPECT_FALSE(pg::log::ValidateLogConfig(config, {.QueueSlotBytes = 1, .FixedBytes = 32768}));
    EXPECT_FALSE(pg::log::ValidateLogConfig(config, {.QueueSlotBytes = sizeof(pg::log::LogRecord), .FixedBytes = 1}));
}
