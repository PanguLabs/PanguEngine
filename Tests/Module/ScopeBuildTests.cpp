#include "Error.h"
#include "ScopeAccess.h"
#include "ScopeIdentity.h"
#include <ModuleDeclaration.h>
#include <ModuleError.h>
#include <ModuleIdentity.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>

#include <cstddef>
#include <expected>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <utility>

namespace
{
using namespace pg::module;
constexpr DeclarationId kFirst{.Bytes = {std::byte{1}}};
constexpr DeclarationId kDependency{.Bytes = {std::byte{2}}};

struct Object
{
    int* Destroyed;
    int* Started;
    int Value;
    Object(int* destroyed, int* started, int value)
        : Destroyed(destroyed)
        , Started(started)
        , Value(value)
    {
    }
    ~Object() { ++*Destroyed; }
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&) = delete;
    Object& operator=(Object&&) = delete;
};

// 复用真实协调器的适配边界，验证拥有模型、hook、类型检查与错误锁存。
struct Sink final : detail::ScopeBuildSink
{
    std::unique_ptr<detail::ErasedScopeObject> Object;
    std::shared_ptr<detail::ScopeObjectControl> Control;
    std::optional<pg::Error> Failure;
    std::unique_ptr<detail::ErasedScopeObject> Provider;
    [[nodiscard]] ModuleId Owner() const noexcept override { return {.Bytes = {std::byte{9}}}; }
    [[nodiscard]] ScopeHandle Target() const noexcept override { return {.Domain = 1, .Slot = 2, .Generation = 1}; }
    pg::Error Reject(pg::Error error) noexcept override
    {
        if (!Failure)
        {
            Failure = error;
        }
        return *Failure;
    }
    pg::Result<std::shared_ptr<detail::ScopeObjectControl>>
    Stage(std::unique_ptr<detail::ErasedScopeObject> object) noexcept override
    {
        if (Failure)
        {
            return std::unexpected(*Failure);
        }
        if (Object)
        {
            return std::unexpected(Reject(MakeModuleError(ModuleError::kInvalidDeclaration)));
        }
        Object = std::move(object);
        Control = std::make_shared<detail::ScopeObjectControl>();
        Control->Module = std::make_shared<detail::ScopeGate>();
        Control->Scope = std::make_shared<detail::ScopeGate>();
        Control->Object = Object->Get();
        Control->Type = Object->Type();
        return Control;
    }
    pg::Result<detail::ScopeDependencyObject> Resolve(DeclarationId id) noexcept override
    {
        if (Failure)
        {
            return std::unexpected(*Failure);
        }
        if (id != kDependency || !Provider)
        {
            return std::unexpected(Reject(MakeModuleError(ModuleError::kUndeclaredDependency)));
        }
        return detail::ScopeDependencyObject{.Object = Provider->Get(), .Type = Provider->Type()};
    }
};

// ScopeHooks<Object> 要求准确的可变引用签名。
// NOLINTNEXTLINE(misc-const-correctness)
[[nodiscard]] pg::Status Start(Object& object) noexcept
{
    ++*object.Started;
    return {};
}
void Stop(Object& /*object*/) noexcept {}
[[nodiscard]] pg::Status Poll(Object& /*object*/) noexcept { return {}; }

TEST(ScopeBuild, OwnershipPrecedesStartAndTokensAreInitiallyClosed)
{
    int destroyed = 0;
    int started = 0;
    ScopeAccess<Object> token;
    {
        Sink sink;
        ScopeBuildContext context(sink);
        auto object = std::make_unique<Object>(&destroyed, &started, 17);
        auto owned = context.Own(std::move(object),
                                 ScopeHooks<Object>{.Start = Start, .RequestStop = Stop, .PollStopped = Poll});
        ASSERT_TRUE(owned);
        EXPECT_FALSE(object);
        EXPECT_EQ(started, 0);
        EXPECT_EQ(destroyed, 0);
        EXPECT_FALSE(owned->TryBorrow());
        EXPECT_EQ(context.Scope().Slot, 2);
        token = *owned;
        ASSERT_TRUE(sink.Object);
        EXPECT_TRUE(sink.Object->Start());
        EXPECT_EQ(started, 1);
        sink.Control->Module->Open();
        sink.Control->Scope->Open();
        sink.Control->Gate.Open();
        auto borrow = token.TryBorrow();
        ASSERT_TRUE(borrow);
        EXPECT_EQ((*borrow)->Value, 17);
        borrow->Release();
        sink.Control->Gate.Close();
        sink.Object.reset();
        sink.Control->Object = nullptr;
        EXPECT_EQ(destroyed, 1);
    }
    EXPECT_FALSE(token.TryBorrow());
    EXPECT_EQ(destroyed, 1);
}

TEST(ScopeBuild, InvalidOwnershipAndTypeErrorsAreSticky)
{
    int destroyed = 0;
    int started = 0;
    Sink sink;
    ScopeBuildContext context(sink);
    const auto invalid = context.Own(std::unique_ptr<Object>());
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error(), MakeModuleError(ModuleError::kInvalidDeclaration));
    const auto next = context.Own(std::make_unique<Object>(&destroyed, &started, 1));
    ASSERT_FALSE(next);
    EXPECT_EQ(invalid.error(), next.error());
    EXPECT_EQ(destroyed, 1);
    EXPECT_FALSE(sink.Object);
    Sink second;
    ScopeBuildContext other(second);
    const auto badHooks =
        other.Own(std::make_unique<Object>(&destroyed, &started, 2), ScopeHooks<Object>{.RequestStop = Stop});
    ASSERT_FALSE(badHooks);
    EXPECT_EQ(badHooks.error(), MakeModuleError(ModuleError::kInvalidDeclaration));
    EXPECT_FALSE(second.Object);
    EXPECT_EQ(destroyed, 2);
    Sink third;
    ScopeBuildContext dependency(third);
    third.Provider = std::make_unique<detail::ScopeObjectModel<int, std::default_delete<int>>>(std::make_unique<int>(7),
                                                                                               ScopeHooks<int>{});
    const auto wrong = dependency.Dependency<double>(kDependency);
    ASSERT_FALSE(wrong);
    EXPECT_EQ(wrong.error(), MakeModuleError(ModuleError::kTypeMismatch));
    const auto sticky = dependency.Dependency<int>(kDependency);
    ASSERT_FALSE(sticky);
    EXPECT_EQ(sticky.error(), wrong.error());
    Sink missing;
    ScopeBuildContext undeclared(missing);
    const auto rejected = undeclared.Dependency<int>(kFirst);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error(), MakeModuleError(ModuleError::kUndeclaredDependency));
}

TEST(ScopeBuild, FactoryIsReusableAndCreatesSeparateObjects)
{
    int destroyed = 0;
    int started = 0;
    ScopeAccess<Object> first;
    ScopeAccess<Object> second;
    ScopeAccess<Object>* destination = &first;
    ScopeFactory factory(
        [config = std::make_unique<int>(33), &destination, &destroyed,
         &started](ScopeBuildContext& context) noexcept -> pg::Status
        {
            const auto object = context.Own(std::make_unique<Object>(&destroyed, &started, *config));
            if (!object)
            {
                return std::unexpected(object.error());
            }
            *destination = *object;
            return {};
        });
    Sink firstSink;
    Sink secondSink;
    ScopeBuildContext firstContext(firstSink);
    ScopeBuildContext secondContext(secondSink);
    ASSERT_TRUE(factory(firstContext));
    destination = &second;
    ASSERT_TRUE(factory(secondContext));
    ASSERT_TRUE(firstSink.Object);
    ASSERT_TRUE(secondSink.Object);
    EXPECT_NE(firstSink.Object->Get(), secondSink.Object->Get());
    EXPECT_EQ(started, 0);
}

TEST(ScopeBuild, NullFunctionPointerIsAnEmptyFactory)
{
    using Function = pg::Status (*)(ScopeBuildContext&) noexcept;
    ScopeFactory const factory(static_cast<Function>(nullptr));
    EXPECT_FALSE(factory);
}
} // namespace
