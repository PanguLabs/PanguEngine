#include "FailureContext.h"
#include "ScopeIdentity.h"
#include "SharedFixtureContract.h"

#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeFactory.h>
#include <ScopeTestSupport.h>
#include <array>
#include <expected>
#include <memory>
#include <vector>

namespace pg::module::fixture
{
namespace
{
// 仅观察 OS 装卸；业务注册只能经显式 Register 回调发生。
struct NativeLifetime final
{
    NativeLifetime() noexcept { pg::test::Observer().Record(pg::test::Event::kNativeLoad); }
    ~NativeLifetime() { pg::test::Observer().Record(pg::test::Event::kNativeUnload); }
    NativeLifetime(const NativeLifetime&) = delete;
    NativeLifetime& operator=(const NativeLifetime&) = delete;
    NativeLifetime(NativeLifetime&&) = delete;
    NativeLifetime& operator=(NativeLifetime&&) = delete;
};
const NativeLifetime kLifetime;

pg::Status StartProvider(pg::test::ProbeState& state, pg::FailureContext& failure) noexcept
{
    const auto started = pg::test::ProbeHooks().Start(state, failure);
    if (pg::test::Observer().SharedOptions().FailProviderStart)
    {
        const auto error = MakeModuleError(ModuleError::kInvalidState);
        failure.SetPrimary(error, "shared provider start failed");
        return std::unexpected(error);
    }
    return started;
}
pg::Status Register(RegistrationContext& context) noexcept
{
    pg::test::Observer().Record(pg::test::Event::kRegister);
    if (pg::test::Observer().SharedOptions().Services)
    {
        return context.DeclareFactory(
            kSharedProviderDeclaration,
            pg::test::Observer().SharedOptions().ProviderInContext ? pg::service::Scope::kContext
                                                                   : pg::service::Scope::kHost,
            ScopeFactory(
                [](ScopeBuildContext& build) noexcept -> pg::Status
                {
                    if (pg::test::Observer().SharedOptions().FailProviderFactory)
                    {
                        return std::unexpected(MakeModuleError(ModuleError::kInvalidState));
                    }
                    auto hooks = pg::test::ProbeHooks();
                    hooks.Start = StartProvider;
                    auto access =
                        build.Own(std::make_unique<pg::test::ProbeState>("provider", std::vector<int>{42}), hooks);
                    if (!access)
                    {
                        return std::unexpected(access.error());
                    }
                    pg::test::Observer().SetAccess(*access);
                    return {};
                }));
    }
    return context.Declare(kSharedProviderDeclaration, "shared-provider");
}
constexpr ModuleDefinition kDefinition{
    .Descriptor =
        {
            .Id = kSharedProviderId,
            .Version = {.Major = 1, .Minor = 0, .Patch = 0},
            .Dependencies = {},
            .DisplayName = "SharedProvider",
            .BuildIdentity = "shared-fixture",
        },
    .RegisterModule = Register,
};
} // namespace
const ModuleDefinition* QuerySharedProvider() noexcept
{
    static constexpr std::array kCycleDependencies{ModuleDependency{.Id = kSharedConsumerId, .Version = {.Major = 1}}};
    static constexpr ModuleDefinition kCycleDefinition{
        .Descriptor =
            {
                .Id = kSharedProviderId,
                .Version = {.Major = 1},
                .Dependencies = kCycleDependencies,
                .DisplayName = "SharedProviderCycle",
                .BuildIdentity = "shared-fixture",
            },
        .RegisterModule = Register,
    };
    return pg::test::Observer().SharedOptions().MetadataCycle ? &kCycleDefinition : &kDefinition;
}
int SharedProviderValue() noexcept { return 42; }
} // namespace pg::module::fixture
