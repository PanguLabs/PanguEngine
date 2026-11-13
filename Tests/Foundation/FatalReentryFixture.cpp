#include <Check.h>
#include <Error.h>

#include "Emergency.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>

namespace
{
using pg::detail::EmergencySnapshot;
static_assert(std::is_trivially_copyable_v<EmergencySnapshot>);
static_assert(std::is_nothrow_copy_constructible_v<EmergencySnapshot>);
static_assert(sizeof(EmergencySnapshot) <= 512);

struct FixtureState
{
    std::atomic<const EmergencySnapshot*> First;
    std::atomic<std::uint64_t> Allocations;
    std::atomic<bool> Entered;
    std::array<char, 512> Reason{};
    std::string_view Scenario;
    std::source_location ExpectedLocation;
    std::uint64_t Baseline{};
};
// 链接接缝/分配替换不接收用户上下文，测试状态仅存在于此独立映像。
FixtureState gState; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void Require(bool condition)
{
    if (!condition)
    {
        std::_Exit(41);
    }
}

template <std::size_t Size>
std::string_view Text(const std::array<char, Size>& text, std::uint16_t size)
{
    Require(size <= Size);
    return {text.data(), size};
}

void VerifySnapshot(const EmergencySnapshot& snapshot)
{
    Require(gState.Allocations.load(std::memory_order_relaxed) == gState.Baseline);
    const auto reason = Text(snapshot.Reason, snapshot.ReasonSize);
    if (gState.Scenario == "SnapshotUnknown")
    {
        Require(reason.empty() && !snapshot.HasError && !snapshot.HasNative && !snapshot.OriginKnown);
        Require(snapshot.Domain == 0 && snapshot.Code == 0 && snapshot.NativeSource == 0 && snapshot.NativeValue == 0);
        Require(snapshot.Line == 0 && snapshot.Column == 0 && snapshot.FileSize == 0 && snapshot.FunctionSize == 0);
        Require(!snapshot.ReasonTruncated && !snapshot.FileTruncated && !snapshot.FunctionTruncated);
        return;
    }
    Require(snapshot.OriginKnown && snapshot.Line == gState.ExpectedLocation.line());
    Require(snapshot.Column == gState.ExpectedLocation.column());
    Require(snapshot.HasError && snapshot.Domain == 0x1122334455667788ULL && snapshot.Code == 91);
    if (gState.Scenario == "SnapshotWithoutNative")
    {
        Require(!snapshot.HasNative && snapshot.NativeSource == 0 && snapshot.NativeValue == 0);
    }
    else
    {
        Require(snapshot.HasNative && snapshot.NativeSource == 0x8877665544332211ULL && snapshot.NativeValue == -7);
    }
    if (gState.Scenario == "SnapshotTruncated")
    {
        Require(snapshot.ReasonSize == 128 && snapshot.FileSize == 160 && snapshot.FunctionSize == 96);
        Require(snapshot.ReasonTruncated && snapshot.FileTruncated && snapshot.FunctionTruncated);
        Require(Text(snapshot.Function, snapshot.FunctionSize).find("fatal_snapshot_location") !=
                std::string_view::npos);
        Require(std::ranges::all_of(reason, [](char value) { return value == 'r'; }));
        Require(std::ranges::all_of(Text(snapshot.File, snapshot.FileSize), [](char value) { return value == 'f'; }));
    }
    else
    {
        if (gState.Scenario == "SnapshotExactCapacity")
        {
            Require(snapshot.ReasonSize == 128 && !snapshot.ReasonTruncated);
            Require(std::ranges::all_of(reason, [](char value) { return value == 'r'; }));
        }
        else
        {
            Require(reason == "owned reason" && !snapshot.ReasonTruncated);
        }
        Require(!snapshot.FileTruncated && !snapshot.FunctionTruncated);
        Require(Text(snapshot.File, snapshot.FileSize).ends_with("FatalReentryFixture.cpp"));
        Require(Text(snapshot.Function, snapshot.FunctionSize).find("main") != std::string_view::npos);
    }
    const auto copy = snapshot;
    gState.Reason.fill('x');
    if (gState.Scenario == "SnapshotTruncated" || gState.Scenario == "SnapshotExactCapacity")
    {
        Require(std::ranges::all_of(Text(copy.Reason, copy.ReasonSize), [](char value) { return value == 'r'; }));
        Require(
            std::ranges::all_of(Text(snapshot.Reason, snapshot.ReasonSize), [](char value) { return value == 'r'; }));
    }
    else
    {
        Require(Text(copy.Reason, copy.ReasonSize) == "owned reason");
        Require(Text(snapshot.Reason, snapshot.ReasonSize) == "owned reason");
    }
    Require(copy.Domain == 0x1122334455667788ULL && copy.Code == 91);
}

namespace fatal_snapshot_location_capacity_regression_with_a_deliberately_long_qualified_function_identity
{
#line 700 "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
std::source_location LongLocation() noexcept { return std::source_location::current(); }
#line 110 "FatalReentryFixture.cpp"
} // namespace fatal_snapshot_location_capacity_regression_with_a_deliberately_long_qualified_function_identity
} // namespace

// 只替换测试映像的普通 C++ 分配；替换函数须直接用同域 malloc/free，不能使用会递归 new 的拥有容器。
// NOLINTBEGIN(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
void* operator new(std::size_t size)
{
    gState.Allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* allocation = std::malloc(size == 0 ? 1 : size))
    {
        return allocation;
    }
    std::abort();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* allocation) noexcept { std::free(allocation); }
void operator delete[](void* allocation) noexcept { std::free(allocation); }
// NOLINTEND(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)

namespace pg::detail
{
[[noreturn]] void NativeEmergencyTerminate(const EmergencySnapshot& snapshot) noexcept;

// 接缝在最终平台边界读取真实 Fatal 构造的现场；产品没有回调/可配置处理器。
[[noreturn]] void EmergencyTerminate(const EmergencySnapshot& snapshot) noexcept
{
    if (gState.Scenario.starts_with("Snapshot"))
    {
        VerifySnapshot(snapshot);
        std::puts("snapshot verified");
        std::fflush(stdout);
        NativeEmergencyTerminate(snapshot);
    }
    Require(gState.Allocations.load(std::memory_order_relaxed) == gState.Baseline);
    if (!gState.Entered.exchange(true, std::memory_order_acq_rel))
    {
        gState.First.store(&snapshot, std::memory_order_release);
        if (gState.Scenario == "RecursiveFatal")
        {
            pg::Fatal("recursive");
        }
        for (;;)
        {
            std::this_thread::yield();
        }
    }
    const auto* first = gState.First.load(std::memory_order_acquire);
    Require(first != nullptr && first != &snapshot);
    if (gState.Scenario == "RecursiveFatal")
    {
        Require(Text(first->Reason, first->ReasonSize) == "first");
        Require(Text(snapshot.Reason, snapshot.ReasonSize) == "recursive");
    }
    else
    {
        Require(Text(first->Reason, first->ReasonSize) == "blocked first");
        Require(Text(snapshot.Reason, snapshot.ReasonSize) == "second");
    }
    std::puts("second entry");
    std::fflush(stdout);
    NativeEmergencyTerminate(snapshot);
}
} // namespace pg::detail

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        return 2;
    }
    const std::span arguments{argv, static_cast<std::size_t>(argc)};
    // argc 已验证为 3，场景参数位于范围内。
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    gState.Scenario = arguments[1];
    const auto before = gState.Allocations.load(std::memory_order_relaxed);
    auto* probe = ::operator new(8);
    ::operator delete(probe);
    Require(gState.Allocations.load(std::memory_order_relaxed) == before + 1);
    gState.Baseline = before + 1;
    if (gState.Scenario == "SnapshotUnknown")
    {
        pg::Fatal({}, {}, std::source_location{});
    }
    if (gState.Scenario.starts_with("Snapshot"))
    {
        gState.ExpectedLocation = std::source_location::current();
        std::string_view reason;
        if (gState.Scenario == "SnapshotTruncated")
        {
            gState.Reason.fill('r');
            reason = {gState.Reason.data(), gState.Reason.size()};
            gState.ExpectedLocation =
                fatal_snapshot_location_capacity_regression_with_a_deliberately_long_qualified_function_identity::
                    LongLocation();
        }
        else if (gState.Scenario == "SnapshotExactCapacity")
        {
            gState.Reason.fill('r');
            reason = {gState.Reason.data(), 128};
        }
        else
        {
            constexpr std::string_view kReason = "owned reason";
            std::ranges::copy(kReason, gState.Reason.begin());
            reason = {gState.Reason.data(), kReason.size()};
        }
        constexpr auto kDomain = pg::ErrorDomainId::Constant<0x1122334455667788ULL>();
        if (gState.Scenario == "SnapshotWithoutNative")
        {
            pg::Fatal(reason, pg::Error{kDomain, 91}, gState.ExpectedLocation);
        }
        pg::Fatal(reason,
                  pg::Error{kDomain, 91,
                            pg::NativeErrorCode{.Source = pg::NativeErrorSourceId{0x8877665544332211ULL}, .Value = -7}},
                  gState.ExpectedLocation);
    }
    if (gState.Scenario == "RecursiveFatal")
    {
        pg::Fatal("first");
    }
    std::atomic<bool> start{};
    const std::thread first(
        [&]
        {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
            pg::Fatal("blocked first");
        });
    gState.Baseline = gState.Allocations.load(std::memory_order_relaxed);
    start.store(true, std::memory_order_release);
    while (gState.First.load(std::memory_order_acquire) == nullptr)
    {
        std::this_thread::yield();
    }
    pg::Fatal("second");
}
