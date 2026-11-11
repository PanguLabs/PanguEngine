#include "EngineHost.h"
#include "Error.h"
#include "HostOperation.h"
#include "HostTestState.h"
#include "MetaActor.h"
#include "MetaModule.h"
#include "MetaTestFixture.h"
#include "ModuleMetaBorrow.h"
#include "Result.h"
#include <HostError.h>
#include <HostMetaAccess.h>
#include <ModuleError.h>
#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace pg::test::meta_fixture
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
TEST(MetaUnload, MetadataBorrowGatesAndPhysicalUnload)
{
    const auto state = std::make_shared<engine::test::HostTestState>();
    engine::test::SetHostTestState(state);
    engine::EngineHost host;
    engine::HostOperationReport report;
    ExpectError(meta::BorrowTypeInfo<Actor>(host), engine::MakeHostError(engine::HostError::kInvalidState));
    ASSERT_TRUE(Start(host, report));
    auto info = meta::BorrowTypeInfo<Actor>(host);
    ASSERT_TRUE(info);
    const std::string savedName(info->Get()->Name);
    const auto snapshots = host.InspectModules();
    ASSERT_TRUE(snapshots);
    auto moved = std::move(*info);
    EXPECT_FALSE(*info);
    EXPECT_EQ(info->Get(), nullptr);
    module::ModuleMetaBorrow assigned;
    assigned = std::move(moved);
    // 移动源为空是公开契约。
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_FALSE(moved);
    EXPECT_EQ(moved.Get(), nullptr);
    ASSERT_TRUE(host.BeginShutdown(report));
    EXPECT_EQ(host.GetState(), engine::HostState::kClosing);
    ExpectError(meta::BorrowTypeInfo<Actor>(host), engine::MakeHostError(engine::HostError::kInvalidState));
    // 已获得的一次 Pin 覆盖整批遍历；Closing 期间无新借用可成功。
    std::size_t count{};
    for (const auto& field : assigned.Get()->Fields)
    {
        EXPECT_FALSE(field.Name.empty());
        ++count;
    }
    EXPECT_EQ(count, 8U);
    ExpectError(host.TryFinishShutdown(report), module::MakeModuleError(module::ModuleError::kBusy));
    EXPECT_EQ(state->NativeUnloadCount, 0U);
    assigned.Release();
    assigned.Release();
    EXPECT_EQ(assigned.Get(), nullptr);
    ASSERT_TRUE(host.TryFinishShutdown(report));
#ifdef PG_TEST_MODULAR
    EXPECT_EQ(state->NativeUnloadCount, 1U);
#endif
    EXPECT_EQ(savedName, "pg::test::meta_fixture::Actor");
    bool found{};
    for (const auto& snapshot : *snapshots)
    {
        if (snapshot.Descriptor.Id == kModuleId)
        {
            EXPECT_EQ(snapshot.Descriptor.DisplayName, "MetaFixture");
            found = true;
        }
    }
    EXPECT_TRUE(found);
    engine::test::SetHostTestState({});
}
} // namespace pg::test::meta_fixture
