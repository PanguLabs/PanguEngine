#include "ResourceAccessFixture.h"
#include "ScopeTestSupport.h"
#ifdef PG_TEST_MODULAR
#include "ResourceAccessFixturePath.h"
#endif

#include <Error.h>
#include <ModuleBuiltin.h>
#include <ModuleDeclaration.h>
#include <ModuleError.h>
#include <ModuleManager.h>
#include <ModuleOperation.h>
#include <RegistrationContext.h>
#include <ScopeIdentity.h>
#include <ScopeResourceAccess.h>

#include <gtest/gtest.h>

#include <array>
#include <exception>
#include <optional>
#include <utility>

namespace pg::module::test
{
namespace
{
using namespace resource_access;
using Kind = pg::service::Scope;
using ResourceAccess = detail::ScopeResourceAccess;
using detail::ScopeResourceQuery;

class ScopeResourceAccessTest : public testing::Test
{
protected:
    void SetUp() override
    {
        pg::test::Observer().Reset();
        auto created = ModuleManager::Create(Declarations, BuiltinResidentQueries(), {}, Report);
        ASSERT_TRUE(created);
        Manager.emplace(std::move(*created));
        for (const auto handle : GetManager().RegistrationOrder())
        {
            ASSERT_TRUE(GetManager().Register(handle, Report));
            ASSERT_TRUE(GetManager().Activate(handle, Report));
        }
#ifdef PG_TEST_MODULAR
        const std::array candidates{
            ModuleLoadCandidate{
                SharedModuleCandidate{
                    .ExpectedId = kModuleId,
                    .Path = kFixturePath,
                },
            },
        };
#else
        const std::array candidates{ModuleLoadCandidate{StaticModuleCandidate{QueryResourceAccessFixture}}};
#endif
        const auto loaded = GetManager().Load(candidates, {}, Report);
        ASSERT_TRUE(loaded);
        Fixture = loaded->Modules.front();
        ASSERT_TRUE(GetManager().Register(Fixture, Report));
    }
    void TearDown() override
    {
        pg::test::Observer().SetNativeUnloadCallback(nullptr, nullptr);
        pg::test::Observer().ConfigureSharedFixture({});
        if (Manager)
        {
            EXPECT_TRUE(GetManager().BeginShutdown(Report));
            EXPECT_TRUE(GetManager().TryFinishShutdown(Report));
        }
    }
    [[nodiscard]] static ScopeResourceQuery Query(ScopeHandle scope, DeclarationId declaration, Kind kind)
    {
        return {.Scope = scope, .Owner = kModuleId, .Declaration = declaration, .Kind = kind};
    }
    void ExpectValue(const ScopeResourceQuery& query, int expected) const
    {
        const auto access = ResourceAccess::Acquire<ScopeValue>(GetManager(), query);
        ASSERT_TRUE(access);
        const auto borrow = access->TryBorrow();
        ASSERT_TRUE(borrow);
        EXPECT_EQ((*borrow)->Value, expected);
    }
    template <class T = ScopeValue>
    void ExpectError(const ScopeResourceQuery& query, ModuleError error) const
    {
        const auto access = ResourceAccess::Acquire<T>(GetManager(), query);
        ASSERT_FALSE(access);
        EXPECT_EQ(access.error(), MakeModuleError(error));
    }
    [[nodiscard]] ModuleManager& GetManager() noexcept
    {
        if (!Manager)
        {
            std::terminate();
        }
        return *Manager;
    }
    [[nodiscard]] const ModuleManager& GetManager() const noexcept
    {
        if (!Manager)
        {
            std::terminate();
        }
        return *Manager;
    }

    // GoogleTest 派生测试体共享句柄与操作报告。
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    ModuleHandle Fixture{};
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
    ModuleOperationReport Report;

private:
    RegistrationScope Declarations;
    std::optional<ModuleManager> Manager;
};

using ScopeResourceAccess = ScopeResourceAccessTest;

// 将身份判断改为按类型选择、忽略 Kind/Domain 或自动查祖先时，本测试必须失败。
TEST_F(ScopeResourceAccess, MetadataAndIdentityAreExact)
{
    ASSERT_TRUE(GetManager().Activate(Fixture, Report));
    const auto host = GetManager().HostScope();
    const auto context = GetManager().CreateScope(Kind::kContext, host, Report);
    ASSERT_TRUE(context);
    const auto world = GetManager().CreateScope(Kind::kWorld, *context, Report);
    ASSERT_TRUE(world);
    ExpectValue(Query(host, kHostDeclaration, Kind::kHost), 1);
    ExpectValue(Query(*context, kContextDeclaration, Kind::kContext), 2);
    ExpectValue(Query(*world, kWorldDeclaration, Kind::kWorld), 3);
    ExpectValue(Query(*context, kSecondContextDeclaration, Kind::kContext), 4);
    const auto made = pg::test::Observer().Count(pg::test::Event::kStart);
    ExpectError(Query(host, kMetadataDeclaration, Kind::kHost), ModuleError::kNotFound);
    auto query = Query(*context, kContextDeclaration, Kind::kContext);
    query.Owner = kModuleRuntimeModuleId;
    ExpectError(query, ModuleError::kNotFound);
    query.Owner = kModuleId;
    ExpectError<int>(query, ModuleError::kTypeMismatch);
    query.Kind = Kind::kWorld;
    ExpectError(query, ModuleError::kInvalidScope);
    query.Kind = static_cast<Kind>(255);
    ExpectError(query, ModuleError::kInvalidScope);
    query.Kind = Kind::kContext;
    ++query.Scope.Domain;
    ExpectError(query, ModuleError::kScopeNotFound);
    query.Scope = *context;
    ++query.Scope.Generation;
    ExpectError(query, ModuleError::kScopeNotFound);
    query.Scope = *context;
    query.Scope.Slot = 0;
    ExpectError(query, ModuleError::kScopeNotFound);
    ExpectError(Query(*world, kHostDeclaration, Kind::kWorld), ModuleError::kNotFound);
    // 常驻 Runtime 没有服务，不能根据同类型业务资源伪造令牌。
    query = Query(host, kHostDeclaration, Kind::kHost);
    query.Owner = kModuleRuntimeModuleId;
    ExpectError(query, ModuleError::kNotFound);
    EXPECT_EQ(pg::test::Observer().Count(pg::test::Event::kStart), made);
}

// 缺少交付、Active、Closing 检查或查询再次运行 Factory 时，本测试必须失败。
TEST_F(ScopeResourceAccess, RejectsUnreadyAndClosing)
{
    const auto host = GetManager().HostScope();
    ExpectError(Query(host, kHostDeclaration, Kind::kHost), ModuleError::kNotFound);
    EXPECT_EQ(pg::test::Observer().Count(pg::test::Event::kStart), 0U);
    ASSERT_TRUE(GetManager().Activate(Fixture, Report));
    pg::test::Observer().ConfigureSharedFixture({.ConsumerBusyPolls = 1, .FailContextFactoryAt = 1});
    const auto failed = GetManager().CreateScope(Kind::kContext, host, Report);
    ASSERT_FALSE(failed);
    const auto undelivered = Report.Scope;
    ASSERT_TRUE(undelivered.IsValid());
    const auto made = pg::test::Observer().Count(pg::test::Event::kStart);
    ExpectError(Query(undelivered, kContextDeclaration, Kind::kContext), ModuleError::kNotReady);
    pg::test::Observer().ConfigureSharedFixture({});
    ASSERT_TRUE(GetManager().PollCleanup(Report));
    const auto context = GetManager().CreateScope(Kind::kContext, host, Report);
    ASSERT_TRUE(context);
    ASSERT_TRUE(GetManager().BeginCloseScope(*context, Report));
    ExpectError(Query(*context, kContextDeclaration, Kind::kContext), ModuleError::kNotReady);
    EXPECT_EQ(pg::test::Observer().Count(pg::test::Event::kStart), made + 2);
    ASSERT_TRUE(GetManager().TryFinishCloseScope(*context, Report));
    auto token = ResourceAccess::Acquire<ScopeValue>(GetManager(), Query(host, kHostDeclaration, Kind::kHost));
    ASSERT_TRUE(token);
    auto borrow = token->TryBorrow();
    ASSERT_TRUE(borrow);
    ASSERT_TRUE(GetManager().BeginUnload(Fixture, Report));
    ExpectError(Query(host, kHostDeclaration, Kind::kHost), ModuleError::kNotReady);
    borrow->Release();
    struct Reentry
    {
        const ModuleManager* Manager{};
        ScopeResourceQuery Query;
        std::optional<pg::Error> Error;
    } reentry{.Manager = &GetManager(), .Query = Query(host, kHostDeclaration, Kind::kHost), .Error = {}};
    // 原生释放仍属于管理操作；即使同时传入无效 Kind，Busy 也必须优先。
    reentry.Query.Kind = static_cast<Kind>(255);
    pg::test::Observer().SetNativeUnloadCallback(
        [](void* context) noexcept
        {
            auto& state = *static_cast<Reentry*>(context);
            const auto result = ResourceAccess::Acquire<ScopeValue>(*state.Manager, state.Query);
            if (!result)
            {
                state.Error = result.error();
            }
        },
        &reentry);
    ASSERT_TRUE(GetManager().TryFinishUnload(Fixture, Report));
    pg::test::Observer().SetNativeUnloadCallback(nullptr, nullptr);
#ifdef PG_TEST_MODULAR
    EXPECT_EQ(reentry.Error, std::optional{MakeModuleError(ModuleError::kBusy)});
#endif
    ASSERT_TRUE(GetManager().BeginShutdown(Report));
    ExpectError(Query(host, kHostDeclaration, Kind::kHost), ModuleError::kInvalidState);
    ASSERT_TRUE(GetManager().TryFinishShutdown(Report));
    ExpectError(Query(host, kHostDeclaration, Kind::kHost), ModuleError::kInvalidState);
}
} // namespace
} // namespace pg::module::test
