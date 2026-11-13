#include "EngineHost.h"
#include "EngineHostTestFixture.h"
#include "Error.h"
#include "HostOperation.h"
#include "ModuleDescriptor.h"
#include "ModuleMetaBorrow.h"
#include "RegistrationContext.h"
#include "Result.h"
#include <HostError.h>
#include <HostMetaAccess.h>
#include <ManualModule.h>
#include <array>
#include <gtest/gtest.h>
#include <optional>
#include <type_traits>
#include <utility>

namespace pg::engine
{
namespace
{
// 致命断言只退出辅助函数，调用者仍执行借用释放与关闭。
template <class T>
void ExpectError(const Result<T>& result, Error expected)
{
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), expected);
}
} // namespace
static_assert(!std::is_copy_constructible_v<module::ModuleMetaBorrow>);
static_assert(std::is_nothrow_move_constructible_v<module::ModuleMetaBorrow>);
template <class T>
concept CanBorrowDescription = requires(const EngineHost& host) { meta::BorrowTypeInfo<T>(host); };
static_assert(CanBorrowDescription<pg::test::ManualType>);
static_assert(!CanBorrowDescription<int>);

namespace
{
struct ReentryFixture
{
    inline static const EngineHost* gHost{};
    inline static std::optional<Error> gReentryError;
    static Status Register(module::RegistrationContext& /*unused*/) noexcept
    {
        const auto result = meta::BorrowTypeInfo<pg::test::ManualType>(*gHost);
        if (!result)
        {
            gReentryError = result.error();
        }
        return {};
    }
    static const module::ModuleDefinition* Query() noexcept
    {
        static const auto kDefinition = []
        {
            auto definition = *pg::test::QueryManualModule();
            definition.RegisterModule = Register;
            return definition;
        }();
        return &kDefinition;
    }
};
} // namespace

TEST(EngineHostMetadata, ReentryBusyPrecedesNotReady)
{
    EngineHost host;
    HostOperationReport report;
    ReentryFixture::gHost = &host;
    ReentryFixture::gReentryError.reset();
    const std::array queries{ReentryFixture::Query};
    ASSERT_TRUE(host.Start({.AdditionalResidentQueries = queries,
                            .ResidentBindings = {},
                            .Modules = {},
                            .ModuleBindings = {},
                            .Logging = pg::engine::test::DisabledLogging()},
                           report));
    EXPECT_EQ(ReentryFixture::gReentryError, MakeHostError(HostError::kBusy));
    ReentryFixture::gHost = nullptr;
    ASSERT_TRUE(host.BeginShutdown(report));
    EXPECT_TRUE(host.TryFinishShutdown(report));
}

TEST(EngineHostMetadata, NoServiceAndClosing)
{
    EngineHost host;
    HostOperationReport report;
    ExpectError(meta::BorrowTypeInfo<pg::test::ManualType>(host), MakeHostError(HostError::kInvalidState));
    const std::array queries{pg::test::QueryManualModule};
    ASSERT_TRUE(host.Start({.AdditionalResidentQueries = queries,
                            .ResidentBindings = {},
                            .Modules = {},
                            .ModuleBindings = {},
                            .Logging = pg::engine::test::DisabledLogging()},
                           report));
    auto info = meta::BorrowTypeInfo<pg::test::ManualType>(host);
    ASSERT_TRUE(info);
    EXPECT_EQ(info->Get()->Name, "pg::test::ManualType");
    ASSERT_TRUE(host.BeginShutdown(report));
    ExpectError(meta::BorrowTypeInfo<pg::test::ManualType>(host), MakeHostError(HostError::kInvalidState));
    EXPECT_FALSE(host.TryFinishShutdown(report));
    auto moved = std::move(*info);
    EXPECT_FALSE(*info);
    EXPECT_EQ(info->Get(), nullptr);
    EXPECT_EQ(moved.Get()->Name, "pg::test::ManualType");
    module::ModuleMetaBorrow assigned;
    assigned = std::move(moved);
    // 借用契约保证移动源可查询且为空。
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_FALSE(moved);
    EXPECT_EQ(moved.Get(), nullptr);
    EXPECT_FALSE(host.TryFinishShutdown(report));
    assigned.Release();
    EXPECT_FALSE(assigned);
    EXPECT_EQ(assigned.Get(), nullptr);
    assigned.Release();
    EXPECT_TRUE(host.TryFinishShutdown(report));
    ExpectError(meta::BorrowTypeInfo<pg::test::ManualType>(host), MakeHostError(HostError::kInvalidState));
}
} // namespace pg::engine
