#include "Fixtures/ScopeAccessFixture.h"
#include "Result.h"
#include <ModuleError.h>
#include <ScopeAccess.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>

#include <expected>
#include <gtest/gtest.h>
#include <memory>
#include <type_traits>
#include <utility>

namespace
{
using namespace pg::module;
using pg::test::ScopeAccessFixture;
static_assert(!std::is_copy_constructible_v<ScopeFactory>);
static_assert(std::is_nothrow_move_constructible_v<ScopeFactory>);
static_assert(!std::is_copy_constructible_v<ScopeBorrow<int>>);
static_assert(std::is_nothrow_move_constructible_v<ScopeBorrow<int>>);
static_assert(!ScopeHandle{}.IsValid());

TEST(ScopeAccess, BorrowPinsAndReleasesOnce)
{
    ScopeAccessFixture const fixture;
    auto result = fixture.Access().TryBorrow();
    ASSERT_TRUE(result);
    EXPECT_EQ(**result, 42);
    EXPECT_EQ(fixture.Control->Gate.Count(), 1);
    EXPECT_EQ(fixture.Control->Module->Count(), 1);
    EXPECT_EQ(fixture.Control->Scope->Count(), 1);
    auto borrow = std::move(*result);
    borrow.Release();
    borrow.Release();
    EXPECT_EQ(fixture.Control->Gate.Count(), 0);
    EXPECT_EQ(fixture.Control->Module->Count(), 0);
    EXPECT_EQ(fixture.Control->Scope->Count(), 0);
}

TEST(ScopeAccess, EveryGateRejectsNewBorrowAndPreservesOldBorrow)
{
    for (int kind = 0; kind < 3; ++kind)
    {
        SCOPED_TRACE(kind);
        ScopeAccessFixture const fixture;
        const auto access = fixture.Access();
        auto admitted = access.TryBorrow();
        ASSERT_TRUE(admitted);
        if (kind == 0)
        {
            fixture.Control->Module->Close();
        }
        else if (kind == 1)
        {
            fixture.Control->Scope->Close();
        }
        else
        {
            fixture.Control->Gate.Close();
        }
        const auto rejected = access.TryBorrow();
        ASSERT_FALSE(rejected);
        EXPECT_EQ(rejected.error(), MakeModuleError(ModuleError::kNotReady));
        EXPECT_EQ(fixture.Control->Gate.Count(), 1);
        EXPECT_EQ(**admitted, 42);
        admitted->Release();
        EXPECT_EQ(fixture.Control->Gate.Count(), 0);
        EXPECT_EQ(fixture.Control->Module->Count(), 0);
        EXPECT_EQ(fixture.Control->Scope->Count(), 0);
        fixture.Control->Object = nullptr;
        EXPECT_FALSE(access.TryBorrow());
    }
}

TEST(ScopeAccess, RejectsEmptyAndWrongType)
{
    const auto empty = ScopeAccess<int>{}.TryBorrow();
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.error(), MakeModuleError(ModuleError::kNotReady));
    ScopeAccessFixture const fixture;
    const auto mismatch = detail::ScopeAccessMaker::Make<double>(fixture.Control);
    ASSERT_FALSE(mismatch);
    EXPECT_EQ(mismatch.error(), MakeModuleError(ModuleError::kTypeMismatch));
}

TEST(ScopeAccess, FactoryOwnsMoveOnlyCapture)
{
    auto configuration = std::make_unique<int>(7);
    ScopeFactory factory(
        [value = std::move(configuration)](ScopeBuildContext& /*context*/) noexcept -> pg::Status
        { return *value == 7 ? pg::Status{} : std::unexpected(MakeModuleError(ModuleError::kInvalidState)); });
    EXPECT_FALSE(configuration);
    ASSERT_TRUE(factory);
    ScopeFactory const moved(std::move(factory));
    // 移出后的拥有型包装器仍有效，但为空。
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_FALSE(factory);
    EXPECT_TRUE(moved);
}
} // namespace
