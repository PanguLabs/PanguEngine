#include <Error.h>
#include <FailureContext.h>
#include <FailureReport.h>

#ifndef PG_FAILURE_REPORT_ALLOCATION_TEST
#include <gtest/gtest.h>
#endif

#include <utility>

#ifdef PG_FAILURE_REPORT_ALLOCATION_TEST
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new> // IWYU pragma: keep

namespace
{
// 仅本测试程序替换分配入口；生产 DLL/引擎分配策略不变。
std::atomic<bool> gTrackAllocations{};    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<unsigned> gAllocationCount{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
} // namespace

// NOLINTBEGIN(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)
void* operator new(std::size_t size)
{
    if (gTrackAllocations.load())
    {
        ++gAllocationCount;
    }
    if (void* allocation = std::malloc(size == 0 ? 1 : size))
    {
        return allocation;
    }
    std::abort();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* allocation) noexcept { std::free(allocation); }
void operator delete[](void* allocation) noexcept { std::free(allocation); }
void operator delete(void* allocation, std::size_t /*size*/) noexcept { std::free(allocation); }
void operator delete[](void* allocation, std::size_t /*size*/) noexcept { std::free(allocation); }
// NOLINTEND(cppcoreguidelines-no-malloc, cppcoreguidelines-owning-memory)

int main()
{
    constexpr auto kDomain = pg::ErrorDomainId::Constant<0xdecafbad11223344ULL>();
    gTrackAllocations = true;
    void* calibration = ::operator new(1);
    ::operator delete(calibration);
    if (gAllocationCount.load() != 1)
    {
        return 2;
    }
    gAllocationCount = 0;
    const bool valid = [kDomain]
    {
        pg::FailureReport report;
        pg::FailureContext context{report};
        context.SetPrimary(pg::Error{kDomain, 17}, "allocation audit");
        context.AddContext("path", "plugin.dll");
        context.AddSecondary(pg::Error{kDomain, 18}, "cleanup");
        context.AddSecondary(pg::FailureSite{pg::Error{kDomain, 19}, "owned cleanup"});
        for (int index = 0; index < 12; ++index)
        {
            context.AddContext("overflow", "context");
        }
        auto copy = report;
        const auto moved = std::move(copy); // NOLINT(performance-move-const-arg): 验证值类型移动路径。
        copy = moved;
        report.Clear();
        auto site = moved.PrimarySite();
        auto secondary = moved.SecondarySite(1);
        if (!site || !secondary)
        {
            return false;
        }
        auto siteCopy = *site;
        const auto siteMoved = std::move(siteCopy); // NOLINT(performance-move-const-arg): 验证值类型移动路径。
        siteCopy = *secondary;
        return siteMoved.Message() == "allocation audit" && copy.Truncated() &&
               secondary.value().Message() == "cleanup";
    }();
    gTrackAllocations = false;
    // 计数已关闭；跨标准库的最小输出不调用格式化库。
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, modernize-use-std-print)
    std::printf("FailureReport=%zu FailureSite=%zu FailureContext=%zu allocations=%u valid=%d\n",
                sizeof(pg::FailureReport), sizeof(pg::FailureSite), sizeof(pg::FailureContext), gAllocationCount.load(),
                static_cast<int>(valid));
    return valid && gAllocationCount.load() == 0 ? 0 : 1;
}
#else
#include <array>
#include <optional>
#include <source_location>
#include <string_view>
#include <type_traits>

namespace
{
constexpr auto kUnknownDomain = pg::ErrorDomainId::Constant<0xdecafbad11223344ULL>();
constexpr pg::Error kPrimary{kUnknownDomain, 17, {.Source = pg::NativeErrorSourceId{77}, .Value = -9}};
constexpr pg::Error kSecondary{kUnknownDomain, 18};

static_assert(std::is_trivially_copyable_v<pg::FailureSite>);
static_assert(std::is_trivially_copyable_v<pg::FailureReport>);
static_assert(std::is_nothrow_copy_constructible_v<pg::FailureReport>);
static_assert(std::is_nothrow_move_constructible_v<pg::FailureReport>);
static_assert(std::is_nothrow_destructible_v<pg::FailureReport>);

[[nodiscard]] pg::FailureSite RequireSite(std::optional<pg::FailureSite> site)
{
    EXPECT_TRUE(site.has_value());
    if (!site)
    {
        return pg::FailureSite{kPrimary, {}, {}};
    }
    return *site;
}

TEST(FailureReport, OwnedSecondaryPreservesOriginalLocation)
{
    const pg::Error error{pg::ErrorDomainId::Constant<0x7461736b39000001ULL>(), 1};
    const pg::FailureSite original{error, "owned secondary"};
    pg::FailureReport report;
    pg::FailureContext context{report};
    context.AddSecondary(original);
    const auto copy = report.SecondarySite(0);
    ASSERT_TRUE(copy);
    EXPECT_EQ(copy->Cause, error);
    EXPECT_EQ(copy->Line(), original.Line());
    EXPECT_EQ(copy->File(), original.File());
    EXPECT_EQ(copy->Function(), original.Function());
}
TEST(FailureReport, PrimarySurvivesOverflow)
{
    pg::FailureReport report;
    pg::FailureContext context{report};
    std::array<char, 2048> text{};
    text.fill('x');
    for (int index = 0; index < 12; ++index)
    {
        context.AddContext("path", {text.data(), text.size()});
        context.AddSecondary(kSecondary, "cleanup", {});
    }
    context.SetPrimary(kPrimary, "original", {});
    const auto primary = report.PrimarySite();
    ASSERT_TRUE(primary.has_value());
    EXPECT_EQ(RequireSite(primary).Cause, kPrimary);
    EXPECT_EQ(RequireSite(primary).Message(), "original");
    EXPECT_TRUE(report.Truncated());
    EXPECT_EQ(report.EntryCount(), 8U);
    EXPECT_LE(sizeof(pg::FailureSite), 512U);
    EXPECT_LE(sizeof(pg::FailureReport), 2048U);
}

TEST(FailureReport, CopiesOwnTheirText)
{
    pg::FailureReport copy;
    std::optional<pg::FailureSite> site;
    {
        std::array<char, 8> text{'o', 'r', 'i', 'g', 'i', 'n', 'a', 'l'};
        pg::FailureReport source;
        pg::FailureContext context{source};
        context.SetPrimary(kPrimary, {text.data(), text.size()});
        context.AddContext("path", "plugin.dll");
        context.AddSecondary(kPrimary, "cleanup");
        copy = source;
        site = source.PrimarySite();
        text.fill('!');
        source.Clear();
    }
    ASSERT_TRUE(copy.PrimarySite().has_value());
    ASSERT_TRUE(site.has_value());
    EXPECT_EQ(RequireSite(copy.PrimarySite()).Message(), "original");
    EXPECT_EQ(RequireSite(site).Message(), "original");
    const auto detached = RequireSite(site);
    const auto reportSite = RequireSite(copy.PrimarySite());
    EXPECT_NE(detached.Message().data(), reportSite.Message().data());
    EXPECT_TRUE(RequireSite(site).OriginKnown());
    EXPECT_TRUE(RequireSite(site).File().ends_with("FailureReportTests.cpp"));
    EXPECT_FALSE(RequireSite(site).Function().empty());
    EXPECT_EQ(copy.ContextKey(0), "path");
    EXPECT_EQ(copy.ContextValue(0), "plugin.dll");
    ASSERT_TRUE(copy.SecondarySite(1).has_value());
    EXPECT_EQ(RequireSite(copy.SecondarySite(1)).Message(), "cleanup");
    EXPECT_EQ(RequireSite(copy.SecondarySite(1)).Cause, kPrimary);
    EXPECT_TRUE(RequireSite(copy.SecondarySite(1)).OriginKnown());
    EXPECT_TRUE(RequireSite(copy.SecondarySite(1)).File().ends_with("FailureReportTests.cpp"));
}

TEST(FailureReport, UnknownOriginIsNotBoundary)
{
    pg::FailureReport report;
    pg::FailureContext context{report};
    context.SetPrimary(kPrimary, "returned without origin", std::source_location{});
    context.AddContext("boundary", "Start");
    context.SetPrimary(kPrimary, "propagation must not replace origin");
    const auto primary = report.PrimarySite();
    ASSERT_TRUE(primary.has_value());
    EXPECT_FALSE(RequireSite(primary).OriginKnown());
    EXPECT_EQ(RequireSite(primary).Line(), 0U);
    EXPECT_EQ(RequireSite(primary).Column(), 0U);
    EXPECT_TRUE(RequireSite(primary).File().empty());
    EXPECT_TRUE(RequireSite(primary).Function().empty());
    EXPECT_EQ(RequireSite(primary).Cause.Domain().Value(), 0xdecafbad11223344ULL);
    EXPECT_EQ(RequireSite(primary).Cause.Native(), kPrimary.Native());
    EXPECT_EQ(RequireSite(primary).Message(), "returned without origin");
    EXPECT_EQ(report.EntryCount(), 1U);
}

TEST(FailureReport, SecondaryCannotReplacePrimary)
{
    pg::FailureReport report;
    pg::FailureContext context{report};
    context.AddSecondary(kSecondary, "earlier cleanup", {});
    EXPECT_FALSE(report.PrimarySite().has_value());
    context.SetPrimary(kPrimary, "original", {});
    context.SetPrimary(kPrimary, "same error", {});
    context.SetPrimary(kSecondary, "conflicting primary", {});
    ASSERT_TRUE(report.PrimarySite().has_value());
    EXPECT_EQ(RequireSite(report.PrimarySite()).Cause, kPrimary);
    EXPECT_EQ(RequireSite(report.PrimarySite()).Message(), "original");
    EXPECT_EQ(report.EntryCount(), 2U);
    ASSERT_TRUE(report.SecondarySite(1).has_value());
    EXPECT_EQ(RequireSite(report.SecondarySite(1)).Cause, kSecondary);
    EXPECT_EQ(RequireSite(report.SecondarySite(1)).Message(), "conflicting primary");
}

TEST(FailureReport, TextBudgetTruncatesBeforeEntryBudget)
{
    pg::FailureReport report;
    pg::FailureContext context{report};
    std::array<char, 1024> text{};
    text.fill('v');
    context.AddContext("k", {text.data(), text.size()});
    EXPECT_EQ(report.EntryCount(), 1U);
    EXPECT_EQ(report.ContextKey(0), "k");
    EXPECT_EQ(report.ContextValue(0).size(), 1023U);
    EXPECT_TRUE(report.Truncated());
    context.AddSecondary(kSecondary, "lost text", {});
    ASSERT_TRUE(report.SecondarySite(1).has_value());
    EXPECT_EQ(RequireSite(report.SecondarySite(1)).Cause, kSecondary);
    EXPECT_TRUE(RequireSite(report.SecondarySite(1)).Message().empty());
    EXPECT_TRUE(RequireSite(report.SecondarySite(1)).Truncated());
    context.SetPrimary(kPrimary, "reserved primary", {});
    EXPECT_EQ(RequireSite(report.PrimarySite()).Message(), "reserved primary");
}

TEST(FailureReport, CopyMoveAndSelfAssignmentKeepOffsetsValid)
{
    pg::FailureReport original;
    pg::FailureContext context{original};
    context.SetPrimary(kPrimary, "origin", {});
    context.AddContext("key", "value");
    context.AddSecondary(kSecondary, "secondary");
    auto copy = original;
    auto moved = std::move(copy); // NOLINT(performance-move-const-arg): 验证值类型移动路径。
    copy = pg::FailureReport{};
    const auto* alias = &moved;
    moved = *alias;
    EXPECT_EQ(RequireSite(moved.PrimarySite()).Message(), "origin");
    EXPECT_EQ(moved.ContextValue(0), "value");
    EXPECT_EQ(RequireSite(moved.SecondarySite(1)).Message(), "secondary");
    pg::FailureReport assigned;
    assigned = std::move(moved); // NOLINT(performance-move-const-arg): 验证值类型移动赋值。
    moved = pg::FailureReport{};
    EXPECT_EQ(RequireSite(assigned.PrimarySite()).Cause, kPrimary);
    EXPECT_EQ(RequireSite(assigned.SecondarySite(1)).Cause, kSecondary);
    auto site = RequireSite(assigned.PrimarySite());
    auto siteCopy = site;
    const auto siteMove = std::move(siteCopy); // NOLINT(performance-move-const-arg): 验证值类型移动路径。
    site = RequireSite(assigned.SecondarySite(1));
    EXPECT_EQ(siteMove.Message(), "origin");
    EXPECT_EQ(siteMove.Cause, kPrimary);
}

TEST(FailureReport, SiteTruncationAndClearAreVisible)
{
    pg::FailureReport report;
    pg::FailureContext context{report};
    std::array<char, 2048> text{};
    text.fill('m');
    context.SetPrimary(kPrimary, {text.data(), text.size()});
    ASSERT_TRUE(report.PrimarySite().has_value());
    EXPECT_TRUE(RequireSite(report.PrimarySite()).Truncated());
    EXPECT_TRUE(report.Truncated());
    report.Clear();
    EXPECT_FALSE(report.PrimarySite().has_value());
    EXPECT_EQ(report.EntryCount(), 0U);
    EXPECT_FALSE(report.Truncated());
    EXPECT_TRUE(report.ContextKey(0).empty());
    EXPECT_TRUE(report.ContextValue(0).empty());
    EXPECT_FALSE(report.SecondarySite(0).has_value());
    context.SetPrimary(kSecondary, "new operation", {});
    EXPECT_EQ(RequireSite(report.PrimarySite()).Cause, kSecondary);
    EXPECT_FALSE(report.Truncated());
}
} // namespace
#endif
