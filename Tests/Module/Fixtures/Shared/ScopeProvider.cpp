#include "FailureContext.h"
#include "ScopeTestSupport.h"
#include "SharedFixtureContract.h"

#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>

#include <expected>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace pg::test
{
// 完整定义和 typeid 实例只存在于业务 DLL，宿主只能保留访问凭据。
struct ProviderPrivateState
{
    ProviderPrivateState() = default;
    ProviderPrivateState(const ProviderPrivateState&) = delete;
    ProviderPrivateState& operator=(const ProviderPrivateState&) = delete;
    ProviderPrivateState(ProviderPrivateState&&) = delete;
    ProviderPrivateState& operator=(ProviderPrivateState&&) = delete;
    std::string Name{"private provider"};
    std::vector<int> Values{7, 8, 9};
    ~ProviderPrivateState() { Observer().Record(Event::kPrivateDestroy); }
};
} // namespace pg::test

namespace pg::module::fixture
{
namespace
{
using pg::test::Event;
using pg::test::Observer;
using pg::test::ProbeState;
struct NativeLifetime final
{
    NativeLifetime() noexcept { Observer().Record(Event::kNativeLoad); }
    ~NativeLifetime() { Observer().Record(Event::kNativeUnload); }
    NativeLifetime(const NativeLifetime&) = delete;
    NativeLifetime& operator=(const NativeLifetime&) = delete;
    NativeLifetime(NativeLifetime&&) = delete;
    NativeLifetime& operator=(NativeLifetime&&) = delete;
};
const NativeLifetime kLifetime;
struct FactoryLifetime final
{
    std::string Name;
    explicit FactoryLifetime(std::string name)
        : Name(std::move(name))
    {
    }
    ~FactoryLifetime() { Observer().Record(Event::kFactoryDestroy); }
    FactoryLifetime(const FactoryLifetime&) = delete;
    FactoryLifetime& operator=(const FactoryLifetime&) = delete;
    FactoryLifetime(FactoryLifetime&&) = delete;
    FactoryLifetime& operator=(FactoryLifetime&&) = delete;
};
struct ProviderState final : ProbeState
{
    ProviderState(std::string name, std::vector<int> values)
        : ProbeState(std::move(name), std::move(values))
    {
    }
    ~ProviderState() { Observer().Record(Event::kProviderDestroy); }
    ProviderState(const ProviderState&) = delete;
    ProviderState& operator=(const ProviderState&) = delete;
    ProviderState(ProviderState&&) = delete;
    ProviderState& operator=(ProviderState&&) = delete;
};
struct ProbeDeleter final
{
    std::string AllocationOwner;
    void operator()(ProbeState* object) const noexcept
    {
        // 此 deleter 仅匹配下方 make_unique<ProviderState> 的分配，保持公开 Own 类型为 ProbeState。
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
        std::unique_ptr<ProviderState> owned(static_cast<ProviderState*>(object));
        owned.reset();
        Observer().Record(Event::kDeleterDestroy);
    }
};
pg::Status Start(ProbeState& state, pg::FailureContext& failure) noexcept
{
    ++state.Starts;
    Observer().Record(Event::kStart);
    if (state.SceneName.starts_with("Context") &&
        std::cmp_equal(Observer().SharedOptions().FailContextStartAt, state.Values.front()))
    {
        const auto error = MakeModuleError(ModuleError::kInvalidState);
        failure.SetPrimary(error, "shared fixture start failed");
        return std::unexpected(error);
    }
    return {};
}
void Stop(ProbeState& state) noexcept
{
    ++state.Stops;
    Observer().Record(Event::kStop);
}
pg::Status Poll(ProbeState& state, pg::FailureContext& /*failure*/) noexcept
{
    ++state.Polls;
    Observer().Record(Event::kPoll);
    return {};
}
ScopeFactory PublicFactory(bool context)
{
    return ScopeFactory(
        [lifetime = std::make_unique<FactoryLifetime>(context ? "Context" : "World"),
         context](ScopeBuildContext& build) noexcept -> pg::Status
        {
            const auto event = context ? Event::kContextCreate : Event::kWorldCreate;
            Observer().Record(event);
            const auto ordinal = static_cast<unsigned>(Observer().Count(event));
            const auto name = lifetime->Name + " " + static_cast<char>('A' + ordinal - 1);
            auto object = std::unique_ptr<ProbeState, ProbeDeleter>(
                std::make_unique<ProviderState>(name, std::vector<int>{static_cast<int>(ordinal), 42}).release(),
                ProbeDeleter{name});
            auto access = build.Own(std::move(object),
                                    ScopeHooks<ProbeState>{.Start = Start, .RequestStop = Stop, .PollStopped = Poll});
            if (!access)
            {
                return std::unexpected(access.error());
            }
            Observer().SaveAccess(build.Scope(), *access);
            if (context && Observer().SharedOptions().FailContextFactoryAt == ordinal)
            {
                return std::unexpected(MakeModuleError(ModuleError::kInvalidState));
            }
            return {};
        });
}
pg::Status Register(RegistrationContext& context) noexcept
{
    Observer().Record(Event::kRegister);
    if (auto status =
            context.DeclareFactory(kScopeContextDeclaration, pg::service::Scope::kContext, PublicFactory(true));
        !status)
    {
        return status;
    }
    if (auto status = context.DeclareFactory(kScopeWorldDeclaration, pg::service::Scope::kWorld, PublicFactory(false));
        !status)
    {
        return status;
    }
    return context.DeclareFactory(
        kScopePrivateDeclaration, pg::service::Scope::kHost,
        ScopeFactory(
            [lifetime = std::make_unique<FactoryLifetime>("private")](ScopeBuildContext& build) noexcept -> pg::Status
            {
                auto object = std::make_unique<pg::test::ProviderPrivateState>();
                object->Name = lifetime->Name;
                const auto access = build.Own(std::move(object));
                if (!access)
                {
                    return std::unexpected(access.error());
                }
                Observer().SavePrivateAccess(*access);
                return {};
            }));
}
constexpr ModuleDefinition kDefinition{
    .Descriptor =
        {
            .Id = kScopeProviderId,
            .Version = {.Major = 1},
            .Dependencies = {},
            .DisplayName = "ScopeProvider",
            .BuildIdentity = "scope-fixture",
        },
    .RegisterModule = Register,
};
} // namespace
const ModuleDefinition* QueryScopeProvider() noexcept { return &kDefinition; }
} // namespace pg::module::fixture
