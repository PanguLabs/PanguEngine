#include "FailureContext.h"
#include "FailureReport.h"
#include "Support/ScopeTestSupport.h"

#include <ModuleDeclaration.h>
#include <ModuleError.h>
#include <ScopeAccess.h>
#include <ScopeBuildContext.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace
{
using namespace pg::module;
using pg::test::ProbeState;
using pg::test::ScopeTestSink;
constexpr DeclarationId kDependency{.Bytes = {std::byte{1}}};

static_assert(!std::is_polymorphic_v<ProbeState>);
static_assert(!std::is_trivially_destructible_v<ProbeState>);
static_assert(!std::is_constructible_v<ScopeAccess<ProbeState>, ScopeAccess<const ProbeState>>);
static_assert(!std::is_constructible_v<ScopeBorrow<ProbeState>, ScopeBorrow<const ProbeState>>);
static_assert(!std::is_constructible_v<ScopeAccess<ProbeState>, std::shared_ptr<detail::ScopeObjectControl>>);
static_assert(std::is_same_v<decltype(std::declval<const detail::ErasedScopeObject&>().Type()), const std::type_info&>);

TEST(ScopeType, NontrivialValueSupportsExactTypeAndReadOnlyDependency)
{
    ScopeTestSink sink;
    pg::FailureReport failureReport;
    pg::FailureContext failureContext(failureReport);
    ScopeBuildContext context(sink, failureContext);
    const auto access =
        context.Own(std::make_unique<ProbeState>("World A", std::vector<int>{1, 2, 3}), pg::test::ProbeHooks());
    ASSERT_TRUE(access);
    EXPECT_EQ(sink.Object->Type(), typeid(ProbeState));
    sink.Publish();
    auto borrowed = access->TryBorrow();
    ASSERT_TRUE(borrowed);
    auto& borrow = *borrowed;
    EXPECT_EQ(borrow->SceneName, "World A");
    EXPECT_EQ(borrow->Values.size(), 3U);
    const auto sameType = context.Dependency<ProbeState>(kDependency);
    ASSERT_TRUE(sameType);
    EXPECT_EQ(*sameType, borrow.Get());
    const auto readOnly = context.Dependency<const ProbeState>(kDependency);
    ASSERT_TRUE(readOnly);
    EXPECT_EQ((*readOnly)->SceneName, "World A");
    const auto wrongType = context.Dependency<int>(kDependency);
    ASSERT_FALSE(wrongType);
    EXPECT_EQ(wrongType.error(), MakeModuleError(ModuleError::kTypeMismatch));
}

struct Base
{
    Base() = default;
    virtual ~Base() = default;
    Base(const Base&) = delete;
    Base& operator=(const Base&) = delete;
    Base(Base&&) = delete;
    Base& operator=(Base&&) = delete;
};
struct Derived final : Base
{
};

TEST(ScopeType, UniquePtrBaseErasesTheDeclaredType)
{
    ScopeTestSink sink;
    pg::FailureReport failureReport;
    pg::FailureContext failureContext(failureReport);
    ScopeBuildContext context(sink, failureContext);
    std::unique_ptr<Base> object = std::make_unique<Derived>();
    const auto owned = context.Own(std::move(object));
    ASSERT_TRUE(owned);
    EXPECT_EQ(sink.Object->Type(), typeid(Base));
    ASSERT_TRUE(context.Dependency<Base>(kDependency));
    const auto wrongType = context.Dependency<Derived>(kDependency);
    ASSERT_FALSE(wrongType);
    EXPECT_EQ(wrongType.error(), MakeModuleError(ModuleError::kTypeMismatch));
}

TEST(ScopeType, ObserverOwnsAccessAfterResourceDestruction)
{
    auto& observer = pg::test::Observer();
    observer.Reset();
    {
        ScopeTestSink sink;
        pg::FailureReport failureReport;
        pg::FailureContext failureContext(failureReport);
        ScopeBuildContext context(sink, failureContext);
        const auto owned =
            context.Own(std::make_unique<ProbeState>("World A", std::vector<int>{1, 2, 3}), pg::test::ProbeHooks());
        ASSERT_TRUE(owned);
        observer.SetAccess(*owned);
        ASSERT_TRUE(sink.Object->Start(failureContext));
        sink.Publish();
        ASSERT_TRUE(observer.Access().TryBorrow());
        sink.Object->RequestStop();
        ASSERT_TRUE(sink.Object->PollStopped(failureContext));
    }
    EXPECT_FALSE(observer.Access().TryBorrow());
    EXPECT_EQ(observer.Events(),
              (std::vector{pg::test::Event::kStart, pg::test::Event::kStop, pg::test::Event::kDestroy}));
    observer.Reset();
}
} // namespace
