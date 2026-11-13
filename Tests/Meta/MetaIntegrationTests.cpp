#include "EngineHost.h"
#include "Error.h"
#include "HostOperation.h"
#include "HostTestState.h"
#include "MetaActor.gen.h"
#include "MetaActor.h"
#include "MetaModule.h"
#include "MetaOptions.h"
#include "MetaService.h"
#include "MetaTestFixture.h"
#include "MetaTraits.h"
#include "ModuleIdentity.h"
#include "ModuleOperation.h"
#include "Result.h"
#include <FoundationModule.h>
#include <HostError.h>
#include <HostMetaAccess.h>
#include <LogModule.h>
#include <MetaIdentity.h>
#include <ModuleBuiltin.h>
#include <ModuleError.h>
#include <ModuleMetadataAccess.h>
#include <RegistrationContext.h>
#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <tuple>

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
TEST(MetaIntegration, NoServiceDescription)
{
    EXPECT_EQ(std::string(module::FormatModuleId(kModuleId).data(), 32), "e1000000000000000000000000000001");
    EXPECT_EQ(std::string(module::FormatModuleId(kServiceModuleId).data(), 32), "e1000000000000000000000000000002");
    engine::EngineHost host;
    engine::HostOperationReport report;
    ASSERT_TRUE(Start(host, report));
    auto info = meta::BorrowTypeInfo<Actor>(host);
    ASSERT_TRUE(info);
    EXPECT_EQ(info->Get()->Name, "pg::test::meta_fixture::Actor");
    EXPECT_FALSE(info->Get()->Id);
    EXPECT_EQ(info->Get()->Fields.size(), 8U);
    auto stable = meta::BorrowTypeInfo<StableActor>(host);
    ASSERT_TRUE(stable);
    const auto id = stable->Get()->Id;
    EXPECT_EQ(id, meta::TypeTraits<StableActor>::Id);
    ASSERT_TRUE(id);
    EXPECT_TRUE(id.value_or(meta::TypeId{}).IsValid());
    constexpr meta::TypeId kExpected{
        .Bytes =
            {
                std::byte{0xe2},
                std::byte{},
                std::byte{},
                std::byte{},
                std::byte{},
                std::byte{},
                std::byte{},
                std::byte{},
                std::byte{},
                std::byte{},
                std::byte{},
                std::byte{},
                std::byte{},
                std::byte{},
                std::byte{},
                std::byte{1},
            },
    };
    EXPECT_EQ(id, kExpected);
    info->Release();
    stable->Release();
    EXPECT_TRUE(Shutdown(host, report));
}

template <class Field, class Value>
concept CanWrite = requires(Field field, Actor& actor, Value value) { field.Write(actor, value); };
static_assert(!CanWrite<std::tuple_element_t<5, decltype(meta::TypeTraits<Actor>::Fields)>, int>);
static_assert(!CanWrite<std::tuple_element_t<6, decltype(meta::TypeTraits<Actor>::Fields)>, int>);

TEST(MetaIntegration, GeneratedFieldsChangeRealObject)
{
    Actor actor;
    const auto& fields = meta::TypeTraits<Actor>::Fields;
    std::get<0>(fields).Write(actor, true);
    std::get<1>(fields).Write(actor, 42);
    std::get<2>(fields).Write(actor, 2.5F);
    std::get<3>(fields).Write(actor, Mood::kMoving);
    std::get<4>(fields).Write(actor, Position{.X = 3, .Y = 4});
    EXPECT_TRUE(actor.Enabled);
    EXPECT_EQ(actor.Health, 42);
    EXPECT_FLOAT_EQ(actor.Speed, 2.5F);
    EXPECT_EQ(actor.State, Mood::kMoving);
    EXPECT_EQ(actor.Location.X, 3);
    EXPECT_EQ(actor.Location.Y, 4);
    EXPECT_EQ(actor.Fixed, 7);
    EXPECT_EQ(actor.Locked, 8);
    actor.SetSecret(19);
    EXPECT_EQ(actor.GetSecret(), 19);
    EXPECT_EQ(std::get<1>(fields).Read(actor), actor.Health);
    std::size_t visited{};
    meta::ForEachField<Actor>(
        [&](const auto& field)
        {
            EXPECT_NE(field.Info.Name, "Secret");
            ++visited;
        });
    EXPECT_EQ(visited, 7U);
    engine::EngineHost host;
    engine::HostOperationReport report;
    ASSERT_TRUE(Start(host, report));
    auto info = meta::BorrowTypeInfo<Actor>(host);
    ASSERT_TRUE(info);
    const auto description = info->Get()->Fields;
    ASSERT_EQ(description.size(), 8U);
    EXPECT_EQ(description.subspan(1).front().Key, "health");
    EXPECT_TRUE(description.subspan(5).front().IsConst);
    EXPECT_FALSE(description.subspan(5).front().IsReadOnly);
    EXPECT_FALSE(description.subspan(6).front().IsConst);
    EXPECT_TRUE(description.subspan(6).front().IsReadOnly);
    EXPECT_EQ(description.subspan(7).front().Visibility, meta::Access::kPrivate);
    info->Release();
    EXPECT_TRUE(Shutdown(host, report));
}

TEST(MetaIntegration, ScopedActorAccess)
{
    engine::EngineHost host;
    engine::HostOperationReport report;
    ASSERT_TRUE(Start(host, report, true));
    auto description = meta::BorrowTypeInfo<ActorService>(host);
    ASSERT_TRUE(description);
    EXPECT_EQ(description->Get()->Name, "pg::test::meta_fixture::ActorService");
    ASSERT_EQ(description->Get()->Fields.size(), 1U);
    EXPECT_EQ(description->Get()->Fields.front().Name, "Value");
    const auto context = host.CreateContext(report);
    ASSERT_TRUE(context);
    const auto access = AcquireActor(host, *context);
    ASSERT_TRUE(access);
    auto borrow = access->TryBorrow();
    ASSERT_TRUE(borrow);
    std::get<1>(meta::TypeTraits<Actor>::Fields).Write((*borrow)->Value, 73);
    EXPECT_EQ((*borrow)->Value.Health, 73);
    EXPECT_TRUE(host.BeginCloseScope(*context, report));
    EXPECT_FALSE(access->TryBorrow());
    ExpectError(host.TryFinishCloseScope(*context, report), module::MakeModuleError(module::ModuleError::kBusy));
    borrow->Release();
    EXPECT_TRUE(host.TryFinishCloseScope(*context, report));
    // 描述 Pin 不能延长服务实例；ScopeBorrow 释放后 Context 可关闭，Module 仍被描述保护。
    EXPECT_FALSE(access->TryBorrow());
    EXPECT_TRUE(host.BeginShutdown(report));
    ExpectError(host.TryFinishShutdown(report), module::MakeModuleError(module::ModuleError::kBusy));
    description->Release();
    EXPECT_TRUE(host.TryFinishShutdown(report));
}

TEST(MetaIntegration, WrongTypeAndReentryBusy)
{
    const auto state = std::make_shared<engine::test::HostTestState>();
    engine::test::SetHostTestState(state);
    engine::EngineHost host;
    engine::HostOperationReport report;
    bool called{};
    state->OnQuery = [&]
    {
        called = true;
        ExpectError(meta::BorrowTypeInfo<Actor>(host), engine::MakeHostError(engine::HostError::kBusy));
    };
    ASSERT_TRUE(Start(host, report));
    EXPECT_TRUE(called);
    ExpectError(engine::detail::HostMetaAccess::Acquire(host, kModuleId, typeid(int)),
                module::MakeModuleError(module::ModuleError::kNotFound));
    state->OnQuery = {};
    EXPECT_TRUE(Shutdown(host, report));
    engine::test::SetHostTestState({});
}

TEST(MetaIntegration, LoadedAndRegisteredAreNotActive)
{
    module::RegistrationScope declarations;
    module::ModuleOperationReport report;
    const std::array queries{module::QueryFoundationModule, pg::log::QueryLogModule, module::QueryModuleRuntime};
    auto manager = module::ModuleManager::Create(declarations, queries, {}, report);
    ASSERT_TRUE(manager);
    const std::array candidates{Candidate(kModuleId)};
    auto loaded = manager->Load(candidates, {}, report);
    ASSERT_TRUE(loaded);
    const auto handle = loaded->Modules.front();
    ExpectError(module::detail::ModuleMetadataAccess::Acquire(*manager, kModuleId, typeid(Actor)),
                module::MakeModuleError(module::ModuleError::kNotReady));
    ASSERT_TRUE(manager->Register(handle, report));
    ExpectError(module::detail::ModuleMetadataAccess::Acquire(*manager, kModuleId, typeid(Actor)),
                module::MakeModuleError(module::ModuleError::kNotReady));
    ASSERT_TRUE(manager->BeginShutdown(report));
    EXPECT_TRUE(manager->TryFinishShutdown(report));
}
} // namespace pg::test::meta_fixture
