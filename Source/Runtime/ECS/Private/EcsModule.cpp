#include <EcsModule.h>

#include <EngineHostModule.h>
#include <FailureContext.h>
#include <ModuleDescriptor.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>
#include <World.h>
#include <array>
#include <expected>
#include <memory>

namespace pg::ecs
{
namespace detail
{
class WorldFactory final
{
public:
    [[nodiscard]] static Status Build(module::ScopeBuildContext& context) noexcept
    {
        // 私有构造只有工厂可见，构造后立即转入独占所有权。
        auto access = context.Own(std::unique_ptr<World>{new World()},
                                  module::ScopeHooks<World>{.RequestStop = RequestStop, .PollStopped = PollStopped});
        if (!access)
        {
            return std::unexpected(access.error());
        }
        return {};
    }

private:
    static void RequestStop(World& world) noexcept { world.Stopping = true; }
    // Scope 在停止后仍会等待全部 Borrow 释放。
    [[nodiscard]] static Status PollStopped(World& /*world*/, FailureContext& /*failure*/) noexcept { return {}; }
};
} // namespace detail

namespace
{
[[nodiscard]] Status Register(module::RegistrationContext& context) noexcept
{
    return context.DeclareFactory(kEcsWorldDeclaration, service::Scope::kWorld,
                                  module::ScopeFactory(detail::WorldFactory::Build), {}, "ECS World");
}

constexpr std::array kDependencies{
    module::ModuleDependency{
        .Id = engine::kEngineHostModuleId,
        .Version = {.Major = 1, .Minor = 0, .Patch = 0},
        .Kind = module::DependencyKind::kRequired,
    },
};
constexpr module::ModuleDefinition kDefinition{
    .Descriptor =
        {
            .Id = kEcsModuleId,
            .Version = {.Major = 1, .Minor = 0, .Patch = 0},
            .ProtocolVersion = 1,
            .Dependencies = kDependencies,
            .DisplayName = "ECS",
            .BuildIdentity = {},
        },
    .RegisterModule = Register,
};
} // namespace

const module::ModuleDefinition* QueryEcsModule() noexcept { return &kDefinition; }
} // namespace pg::ecs
