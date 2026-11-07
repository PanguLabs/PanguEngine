#include "ResourceAccessFixture.h"
#include "ScopeTestSupport.h"

#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>

#include <array>
#include <cstddef>
#include <expected>
#include <memory>

namespace pg::module::test
{
namespace
{
using namespace resource_access;
using pg::test::Event;
using pg::test::Observer;

#ifdef PG_TEST_MODULAR
struct NativeLifetime final
{
    NativeLifetime() = default;
    NativeLifetime(const NativeLifetime&) = delete;
    NativeLifetime& operator=(const NativeLifetime&) = delete;
    NativeLifetime(NativeLifetime&&) = delete;
    NativeLifetime& operator=(NativeLifetime&&) = delete;
    ~NativeLifetime() { Observer().Record(Event::kNativeUnload); }
};
const NativeLifetime kLifetime;
#endif

// 回调签名由 ScopeHooks 固定；此夹具只观察停止阻塞选项。
// NOLINTNEXTLINE(misc-const-correctness)
void Stop([[maybe_unused]] ScopeValue& value) noexcept {}
// NOLINTNEXTLINE(misc-const-correctness)
pg::Status Poll([[maybe_unused]] ScopeValue& value) noexcept
{
    return Observer().SharedOptions().ConsumerBusyPolls != 0
               ? pg::Status{std::unexpected(MakeModuleError(ModuleError::kBusy))}
               : pg::Status{};
}

ScopeFactory Factory(int value)
{
    return ScopeFactory(
        [value](ScopeBuildContext& build) noexcept -> pg::Status
        {
            Observer().Record(Event::kStart);
            if (value == 2)
            {
                Observer().Record(Event::kContextCreate);
            }
            const auto owned = build.Own(std::make_unique<ScopeValue>(ScopeValue{.Value = value}),
                                         ScopeHooks<ScopeValue>{.RequestStop = Stop, .PollStopped = Poll});
            if (!owned)
            {
                return std::unexpected(owned.error());
            }
            if (value == 4 &&
                Observer().SharedOptions().FailContextFactoryAt == Observer().Count(Event::kContextCreate))
            {
                return std::unexpected(MakeModuleError(ModuleError::kInvalidState));
            }
            return {};
        });
}

pg::Status Register(RegistrationContext& context) noexcept
{
    constexpr std::array kDeclarations{
        kHostDeclaration,
        kContextDeclaration,
        kWorldDeclaration,
        kSecondContextDeclaration,
    };
    constexpr std::array kKinds{
        pg::service::Scope::kHost,
        pg::service::Scope::kContext,
        pg::service::Scope::kWorld,
        pg::service::Scope::kContext,
    };
    for (std::size_t index = 0; index < kDeclarations.size(); ++index)
    {
        if (const auto status =
                context.DeclareFactory(kDeclarations.at(index), kKinds.at(index), Factory(static_cast<int>(index + 1)));
            !status)
        {
            return status;
        }
    }
    return context.Declare(kMetadataDeclaration, "仅元数据");
}
constexpr ModuleDefinition kDefinition{
    .Descriptor =
        {
            .Id = kModuleId,
            .Version = {.Major = 1},
            .Dependencies = {},
            .DisplayName = "ScopeResourceFixture",
            .BuildIdentity = "resource-access-fixture",
        },
    .RegisterModule = Register,
};
} // namespace
const ModuleDefinition* QueryResourceAccessFixture() noexcept { return &kDefinition; }
} // namespace pg::module::test
