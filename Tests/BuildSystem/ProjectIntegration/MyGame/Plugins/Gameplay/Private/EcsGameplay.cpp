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
#include <cstddef>
#include <expected>
#include <memory>

namespace pg::fixture::ecs
{
namespace
{
struct OffsetTag
{
    int Value{};
};

struct CallbackLifetime
{
    explicit CallbackLifetime(WorldEvents events, std::size_t WorldTrace::* event) noexcept
        : Events(events)
        , Event(event)
    {
    }
    ~CallbackLifetime() { Events.Record(Event); }
    CallbackLifetime(const CallbackLifetime&) = delete;
    CallbackLifetime& operator=(const CallbackLifetime&) = delete;
    CallbackLifetime(CallbackLifetime&&) = delete;
    CallbackLifetime& operator=(CallbackLifetime&&) = delete;

    WorldEvents Events;
    std::size_t WorldTrace::* Event;
};

#ifdef PANGU_FIXTURE_MODULAR
// 应用的观测状态覆盖同一动态库最后一次原生释放。
struct NativeLifetime
{
    NativeLifetime() noexcept = default;
    ~NativeLifetime()
    {
        if (Trace != nullptr)
        {
            Trace->GameplayNativeUnload = ++Trace->Sequence;
        }
    }
    NativeLifetime(const NativeLifetime&) = delete;
    NativeLifetime& operator=(const NativeLifetime&) = delete;
    NativeLifetime(NativeLifetime&&) = delete;
    NativeLifetime& operator=(NativeLifetime&&) = delete;

    EcsFixtureTrace* Trace{};
};
[[nodiscard]] NativeLifetime& GameplayNativeLifetime() noexcept
{
    static NativeLifetime lifetime;
    return lifetime;
}
#endif

// NOLINTBEGIN(misc-include-cleaner) PanguFlecs.h 提供未标注重导出的 flecs C++ 接口。
void InstallCallbacks(const pg::flecs::world& world, WorldEvents events)
{
    // flecs 持有两个闭包的唯一所有权。安装资源退出后，闭包依然访问 App 的观测状态。
    world.observer<Position>()
        .event(pg::flecs::OnAdd)
        .each([lifetime = std::make_unique<CallbackLifetime>(events, &WorldTrace::ObserverContextDestroyed)](
                  const Position& /*position*/) noexcept { ++lifetime->Events.World->ObserverCalls; });
    world.system<Position, const Velocity>().each(
        [lifetime = std::make_unique<CallbackLifetime>(events, &WorldTrace::SystemContextDestroyed)](
            pg::flecs::iter& iter, std::size_t /*row*/, Position& position, const Velocity& velocity) noexcept
        {
            position.Value += velocity.Value * iter.delta_time();
            ++lifetime->Events.World->SystemCalls;
        });
}
// NOLINTEND(misc-include-cleaner)

[[nodiscard]] Status Build(module::ScopeBuildContext& build) noexcept
{
    const auto world = build.Dependency<pg::ecs::World>(pg::ecs::kEcsWorldDeclaration);
    if (!world)
    {
        return std::unexpected(world.error());
    }
    const auto resource = build.Dependency<TraceResource>(kTraceResource);
    if (!resource)
    {
        return std::unexpected(resource.error());
    }
    auto& trace = *(*resource)->Trace;
#ifdef PANGU_FIXTURE_MODULAR
    auto& lifetime = GameplayNativeLifetime();
    Check(lifetime.Trace == nullptr || lifetime.Trace == &trace, "Gameplay 的原生持有应共享 App 观测状态");
    lifetime.Trace = &trace;
#endif
    const auto index = trace.WorldCount++;
    Check(index < trace.Worlds.size(), "World 观测槽不足");
    const WorldEvents events{.Trace = &trace, .World = &trace.Worlds.at(index)};
    auto& native = (*world)->Native();
    if (index == 1)
    {
        native.component<OffsetTag>("EcsFixtureOffsetTag");
    }
    const auto position = native.component<Position>(kPositionName);
    native.component<Velocity>();
    native.component<Tracks>();
    BindMotionState(native);
    native.component<Observation>();
    native.set(Observation{
        .ProviderId = position.id(),
        .IdleConstant = native.entity(MotionState::kIdle).id(),
        .MovingConstant = native.entity(MotionState::kMoving).id(),
        .OwnerDtorBefore = native.type_info<Position>()->hooks.dtor,
    });

    // 哨兵只验证 World 析构，应用实体才触发下面登记的 OnAdd。
    native.entity(kStateEntity)
        .add(MotionState::kIdle)
        .emplace<Position>(Position{.Lifetime = std::make_unique<ComponentLifetime>(events)});
    InstallCallbacks(native, events);

    const auto install = build.Own(std::make_unique<GameplayInstall>(events));
    return install ? Status{} : std::unexpected(install.error());
}

[[nodiscard]] Status Register(module::RegistrationContext& context) noexcept
{
    constexpr std::array kWorldDependencies{
        module::ScopeDependency{.Id = pg::ecs::kEcsWorldDeclaration, .Scope = service::Scope::kWorld},
        module::ScopeDependency{.Id = kTraceResource, .Scope = service::Scope::kHost},
    };
    return context.DeclareFactory(kGameplayInstall, service::Scope::kWorld, module::ScopeFactory(Build),
                                  kWorldDependencies, "ECS Gameplay installation");
}

constexpr std::array kDependencies{
    module::ModuleDependency{.Id = pg::ecs::kEcsModuleId, .Version = {.Major = 1}},
    module::ModuleDependency{.Id = kTraceModuleId, .Version = {.Major = 1}},
};
constexpr module::ModuleDefinition kDefinition{
    .Descriptor =
        {
            .Id = kEcsGameplayId,
            .Version = {.Major = 1},
            .Dependencies = kDependencies,
            .DisplayName = "ECS Gameplay",
            .BuildIdentity = {},
        },
    .RegisterModule = Register,
};
} // namespace

const module::ModuleDefinition* QueryEcsGameplay() noexcept { return &kDefinition; }
} // namespace pg::fixture::ecs
