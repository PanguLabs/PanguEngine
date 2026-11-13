#include <Check.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <span>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include <stdio.h> // NOLINT(modernize-deprecated-headers): _lock_file 是 Windows CRT 扩展。
#endif

namespace
{

// atexit 不接收上下文参数，测试进程只在启动时设置一次。
const char* gMarker{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void MarkCleanup() { const std::ofstream marker{gMarker}; }
struct Cleanup
{
    Cleanup() = default;
    Cleanup(const Cleanup&) = delete;
    Cleanup& operator=(const Cleanup&) = delete;
    Cleanup(Cleanup&&) = delete;
    Cleanup& operator=(Cleanup&&) = delete;
    ~Cleanup() { MarkCleanup(); }
};

bool Condition(bool result)
{
    std::puts("condition");
    std::fflush(stdout);
    return result;
}
std::string_view Message()
{
    std::puts("message");
    std::fflush(stdout);
    return "fixture";
}
} // namespace

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        return 2;
    }
    const std::span arguments{argv, static_cast<std::size_t>(argc)};
    // argc 已验证为 3，以下两个索引均在参数范围内。
    gMarker = arguments[2]; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    const std::string_view scenario{
        arguments[1]}; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    if (scenario == "Evaluation")
    {
        PG_ASSERT(Condition(true), Message());
        PG_CHECK(Condition(true), Message());
        std::puts("returned");
        return 0;
    }
    if (scenario == "AssertFailure")
    {
        PG_ASSERT(Condition(false), Message());
        std::puts("returned");
        return 0;
    }
    if (std::atexit(MarkCleanup) != 0)
    {
        return 3;
    }
    [[maybe_unused]] const Cleanup cleanup;
    if (scenario == "CheckFailure")
    {
        PG_CHECK(Condition(false), Message());
    }
    if (scenario == "BlockedOutput")
    {
        std::atomic<bool> outputLocked{};
        const std::thread blocker(
            [&]
            {
#ifdef _WIN32
                _lock_file(stderr);
#else
                flockfile(stderr);
#endif
                outputLocked.store(true, std::memory_order_release);
                for (;;)
                {
                    std::this_thread::yield();
                }
            });
        while (!outputLocked.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        pg::Fatal({}, {}, std::source_location{});
    }
    if (scenario == "ConcurrentFatal")
    {
        std::atomic<bool> start{};
        const std::thread first(
            [&]
            {
                while (!start.load(std::memory_order_acquire))
                {
                    std::this_thread::yield();
                }
                pg::Fatal("first");
            });
        start.store(true, std::memory_order_release);
        pg::Fatal("second");
    }
    pg::Fatal({}, {}, std::source_location{});
}
