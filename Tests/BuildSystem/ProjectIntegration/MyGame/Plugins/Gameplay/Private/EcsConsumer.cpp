#include <EcsFixtureTypes.h>

#include <EcsModule.h>
#include <ModuleDescriptor.h>
#include <PanguFlecs.h> // IWYU pragma: keep
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>
#include <World.h>
#include <array>
#include <expected>
#include <memory>
#include <utility>

namespace pg::fixture::ecs
{
namespace
{
struct ConsumerInstall
{
};

[[nodiscard]] Status Build(module::ScopeBuildContext& build) noexcept
{
    const auto gameplay = build.Dependency<GameplayInstall>(kGameplayInstall);
    if (!gameplay)
    {
        return std::unexpected(gameplay.error());
    }
    const auto world = build.Dependency<pg::ecs::World>(pg::ecs::kEcsWorldDeclaration);
    if (!world)
    {
        return std::unexpected(world.error());
    }
    auto& native = (*world)->Native();
    const auto cachedBefore = native.id_if_registered<Position>();
    const auto namedPosition = native.lookup(kPositionName);
    Check(namedPosition.is_alive(), "Consumer 必须在绑定前找到 Gameplay 登记的组件");
    const auto position = native.component<Position>();
    Check(position.id() == namedPosition.id(), "Consumer 必须复用已登记的命名组件");

    BindMotionState(native);
    native.component<Observation>();
    auto& observation = native.get_mut<Observation>();
    observation.ConsumerBefore = cachedBefore;
    observation.ConsumerId = native.id_if_registered<Position>();
    observation.OwnerDtorAfter = native.type_info<Position>()->hooks.dtor;
    observation.ConsumerIdleConstant = native.entity(MotionState::kIdle).id();
    observation.ConsumerMovingConstant = native.entity(MotionState::kMoving).id();
    const auto state = native.lookup(kStateEntity);
    observation.ConsumerSawIdle =
        state.has(MotionState::kIdle) && state.target<MotionState>().id() == observation.ConsumerIdleConstant;

    if (std::exchange((*gameplay)->Events.Trace->FailNextWorld, false))
    {
        return std::unexpected(kInjectedFailure);
    }
    const auto install = build.Own(std::make_unique<ConsumerInstall>());
    return install ? Status{} : std::unexpected(install.error());
}

[[nodiscard]] Status Register(module::RegistrationContext& context) noexcept
{
    constexpr std::array kWorldDependencies{
        module::ScopeDependency{.Id = kGameplayInstall, .Scope = service::Scope::kWorld},
        module::ScopeDependency{.Id = pg::ecs::kEcsWorldDeclaration, .Scope = service::Scope::kWorld},
    };
    return context.DeclareFactory(kConsumerInstall, service::Scope::kWorld, module::ScopeFactory(Build),
                                  kWorldDependencies, "ECS Consumer installation");
}

constexpr std::array kDependencies{
    module::ModuleDependency{.Id = kEcsGameplayId, .Version = {.Major = 1}},
    module::ModuleDependency{.Id = pg::ecs::kEcsModuleId, .Version = {.Major = 1}},
};
constexpr module::ModuleDefinition kDefinition{
    .Descriptor =
        {
            .Id = kEcsConsumerId,
            .Version = {.Major = 1},
            .Dependencies = kDependencies,
            .DisplayName = "ECS Consumer",
            .BuildIdentity = {},
        },
    .RegisterModule = Register,
};
} // namespace

const module::ModuleDefinition* QueryEcsConsumer() noexcept { return &kDefinition; }
} // namespace pg::fixture::ecs
