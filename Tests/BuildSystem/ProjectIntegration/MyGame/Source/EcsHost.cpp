#include "EcsHost.h"

#ifdef PANGU_FIXTURE_MODULAR
#include "EcsCandidates.h"
#else
#include <GeneratedModuleCatalog.h>
#endif

#include <EcsFixtureTypes.h>
#include <EcsModule.h>
#include <EngineHost.h>
#include <HostOperation.h>
#include <HostStartupConfig.h>
#include <LogTypes.h>
#include <ModuleDescriptor.h>
#include <ModuleManager.h>
#include <PanguFlecs.h> // IWYU pragma: keep
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeAccess.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>
#include <World.h>
#include <WorldAccess.h>
#include <array>
#include <cstdio>
#include <expected>
#include <memory>
#include <span>

namespace pg::fixture::ecs
{
namespace
{
[[nodiscard]] EcsFixtureTrace& Trace() noexcept
{
    // 本进程只运行一组场景；观测状态覆盖所有 Host 的最后一次原生释放。
    static EcsFixtureTrace trace;
    return trace;
}

[[nodiscard]] Status RegisterTrace(module::RegistrationContext& context) noexcept
{
    return context.DeclareFactory(kTraceResource, service::Scope::kHost,
                                  module::ScopeFactory(
                                      [](module::ScopeBuildContext& build) noexcept -> Status
                                      {
                                          const auto owned = build.Own(
                                              std::make_unique<TraceResource>(TraceResource{.Trace = &Trace()}));
                                          return owned ? Status{} : std::unexpected(owned.error());
                                      }),
                                  {}, "ECS fixture trace");
}

[[nodiscard]] const module::ModuleDefinition* QueryTrace() noexcept
{
    static constexpr module::ModuleDefinition kDefinition{
        .Descriptor =
            {
                .Id = kTraceModuleId,
                .Version = {.Major = 1},
                .Dependencies = {},
                .DisplayName = "ECS fixture trace",
                .BuildIdentity = {},
            },
        .RegisterModule = RegisterTrace,
    };
    return &kDefinition;
}

[[nodiscard]] auto Candidates()
{
#ifdef PANGU_FIXTURE_MODULAR
    return EcsCandidates();
#else
    const auto entries = module::GeneratedStaticModuleCatalog().Entries;
    Check(entries.size() == 2, "ECS 业务目录必须包含 Gameplay 和 Consumer");
    return std::array<module::ModuleLoadCandidate, 2>{
        module::StaticModuleCandidate{entries.front().Query},
        module::StaticModuleCandidate{entries.back().Query},
    };
#endif
}

void Start(engine::EngineHost& host, std::span<const module::ModuleLoadCandidate> candidates,
           engine::HostOperationReport& report)
{
    const std::array<module::QueryModuleFn, 2> residentQueries{pg::ecs::QueryEcsModule, QueryTrace};
    const engine::HostStartupConfig config{
        .AdditionalResidentQueries = residentQueries,
        .ResidentBindings = {},
        .Modules = candidates,
        .ModuleBindings = {},
        .Logging = {.Directory = {}, .Enabled = log::OutputMask::kNone, .Required = log::OutputMask::kNone},
    };
    Check(host.Start(config, report).has_value(), "ECS Project Host 启动失败");
}

[[nodiscard]] module::ScopeHandle CreateContext(engine::EngineHost& host, engine::HostOperationReport& report)
{
    const auto context = host.CreateContext(report);
    Check(context.has_value(), "ECS Context 创建失败");
    return *context;
}

[[nodiscard]] module::ScopeHandle CreateWorld(engine::EngineHost& host, module::ScopeHandle context,
                                              engine::HostOperationReport& report)
{
    const auto world = host.CreateWorld(context, report);
    Check(world.has_value(), "ECS World 创建失败");
    return *world;
}

[[nodiscard]] auto BorrowWorld(const engine::EngineHost& host, module::ScopeHandle world)
{
    const auto access = pg::ecs::AcquireWorld(host, world);
    Check(access.has_value(), "Ready World 缺少 ECS 资源");
    return access->TryBorrow();
}

// NOLINTBEGIN(misc-include-cleaner) PanguFlecs.h 提供未标注重导出的 flecs C++ 接口。
struct WorldResult
{
    pg::flecs::entity_t Entity;
    pg::flecs::entity_t PositionId;
};
// NOLINTEND(misc-include-cleaner)

[[nodiscard]] WorldResult RunWorld(const engine::EngineHost& host, module::ScopeHandle world, const WorldTrace& trace)
{
    const auto borrow = BorrowWorld(host, world);
    Check(borrow.has_value(), "Ready World 的 ECS 借用失败");
    const auto& native = (*borrow)->Native();
    const auto position = native.component<Position>();
    native.component<Velocity>();
    native.component<Tracks>();
    BindMotionState(native);
    native.component<Observation>();

    const auto& observation = native.get<Observation>();
    const auto providerId = observation.ProviderId;
    const auto consumerId = observation.ConsumerId;
    Check(providerId != 0 && providerId == consumerId && position.id() == providerId, "同一 World 的组件 ID 必须一致");
    Check(observation.ConsumerBefore == 0 || observation.ConsumerBefore == providerId,
          "Consumer 绑定之前允许有共享 cache，已有绑定必须属于当前 World");
    Check(observation.OwnerDtorBefore != nullptr && observation.OwnerDtorBefore == observation.OwnerDtorAfter,
          "Consumer 必须保留 Gameplay 安装的析构 Hook");
    Check(observation.OwnerDtorBefore == (*borrow)->Native().type_info<Position>()->hooks.dtor,
          "App 必须保留 Gameplay 安装的析构 Hook");
    const auto idle = native.entity(MotionState::kIdle);
    const auto moving = native.entity(MotionState::kMoving);
    Check(idle.is_alive() && moving.is_alive() && idle.id() != moving.id() && idle.id() == observation.IdleConstant &&
              idle.id() == observation.ConsumerIdleConstant && moving.id() == observation.MovingConstant &&
              moving.id() == observation.ConsumerMovingConstant && observation.ConsumerSawIdle,
          "各模块必须复用同一 World 的枚举常量实体与既有状态");

    const auto state = native.lookup(kStateEntity);
    Check(state.has(MotionState::kIdle) && state.target<MotionState>().id() == idle.id(),
          "App 绑定枚举后必须保留 Gameplay 写入的状态");
    const auto entity = native.entity();
    entity.emplace<Position>(Position{.Value = 7, .Lifetime = {}})
        .set(Velocity{.Value = 4})
        .add<Tracks>(state)
        .add(MotionState::kMoving);
    Check(entity.target<Tracks>().id() == state.id() && entity.target<MotionState>().id() == moving.id(),
          "App 应能通过显式绑定写入关系和枚举状态");

    const auto advanced = (*borrow)->Progress(0.25);
    Check(advanced && *advanced, "ECS World 推进失败");
    const auto query = native.query<const Position, const Velocity>();
    double positionAfter{};
    query.each([&positionAfter](const Position& position, const Velocity& /*velocity*/)
               { positionAfter = position.Value; });
    Check(query.count() == 1 && positionAfter == 8 && trace.ObserverCalls == 1 && trace.SystemCalls == 1,
          "推进应得到位置 8，Observer 和 System 各执行一次");
    return {.Entity = entity.id(), .PositionId = providerId};
}

void VerifyStillUsable(const engine::EngineHost& host, module::ScopeHandle world, WorldResult initial,
                       const WorldTrace& trace, int expectedPosition, int expectedTicks)
{
    const auto borrow = BorrowWorld(host, world);
    Check(borrow.has_value(), "其他 World 关闭后当前 World 应仍可借用");
    const auto entity = (*borrow)->Native().entity(initial.Entity);
    Check(entity.is_alive() && entity.get<Position>().Value == expectedPosition,
          "其他 World 的清理不能更改当前 World 的数据");
    const auto advanced = (*borrow)->Progress(0.25);
    Check(advanced && *advanced && entity.get<Position>().Value == expectedPosition + 1 && trace.ObserverCalls == 1 &&
              trace.SystemCalls == expectedTicks,
          "其他 World 的清理不能破坏当前 World 的回调");
}

[[nodiscard]] bool WorldDestroyed(const WorldTrace& trace)
{
    return trace.InstallDestroyed != 0 && trace.InstallDestroyed < trace.ComponentDestroyed &&
           trace.InstallDestroyed < trace.ObserverContextDestroyed &&
           trace.InstallDestroyed < trace.SystemContextDestroyed;
}

[[nodiscard]] bool WorldLive(const WorldTrace& trace)
{
    return trace.InstallDestroyed == 0 && trace.ComponentDestroyed == 0 && trace.ObserverContextDestroyed == 0 &&
           trace.SystemContextDestroyed == 0;
}

void Shutdown(engine::EngineHost& host, engine::HostOperationReport& report)
{
    Check(host.BeginShutdown(report) && host.TryFinishShutdown(report) && host.GetState() == engine::HostState::kClosed,
          "ECS Project Host 关闭失败");
}

void Run()
{
    auto& trace = Trace();
    const auto candidates = Candidates();
    engine::EngineHost host;
    engine::EngineHost otherHost;
    engine::HostOperationReport report;
    Start(host, candidates, report);
    const auto context = CreateContext(host, report);
    const auto first = CreateWorld(host, context, report);
    const auto second = CreateWorld(host, context, report);
    Start(otherHost, candidates, report);
    const auto other = CreateWorld(otherHost, CreateContext(otherHost, report), report);
    const auto& [worldA, worldB, otherWorld, failedWorld] = trace.Worlds;
    const auto firstResult = RunWorld(host, first, worldA);
    const auto secondResult = RunWorld(host, second, worldB);
    const auto otherResult = RunWorld(otherHost, other, otherWorld);
    Check(firstResult.PositionId != secondResult.PositionId, "两个 World 应允许不同的本地组件 ID");
    std::puts("ECS Position binding and owner dtor preserved");
    std::puts("ECS Progress position=8 observer=1");

    Check(host.BeginCloseScope(first, report) && host.TryFinishCloseScope(first, report), "World A 单独关闭失败");
    Check(WorldDestroyed(worldA) && WorldLive(worldB) && WorldLive(otherWorld) && trace.GameplayNativeUnload == 0 &&
              !pg::ecs::AcquireWorld(host, first),
          "World A 必须完成清理，Gameplay 与两个存活 World 应仍然有效");
    VerifyStillUsable(host, second, secondResult, worldB, 8, 2);
    VerifyStillUsable(otherHost, other, otherResult, otherWorld, 8, 2);
    std::puts("ECS World A destroyed; World B usable");

    trace.FailNextWorld = true;
    const auto failed = host.CreateWorld(context, report);
    Check(!failed && failed.error() == kInjectedFailure && !trace.FailNextWorld && WorldDestroyed(failedWorld) &&
              WorldLive(worldB) && WorldLive(otherWorld) && trace.GameplayNativeUnload == 0,
          "Consumer 注入失败应回滚已安装 Gameplay 回调的 World");
    VerifyStillUsable(host, second, secondResult, worldB, 9, 3);
    VerifyStillUsable(otherHost, other, otherResult, otherWorld, 9, 3);
    std::puts("ECS Failed World rolled back; World B usable");
    std::puts("ECS Second Host usable after World close and rollback");

    Check(trace.WorldCount == trace.Worlds.size(), "应观察到两个 Host 的四个 World 工厂调用");
    // World B 保持打开，验证宿主整体关闭路径主动排空它。
    Shutdown(host, report);
    Check(WorldDestroyed(worldB) && WorldLive(otherWorld) && trace.GameplayNativeUnload == 0,
          "关闭第一个 Host 必须排空 World B 并保留仍由另一个 Host 持有的 Gameplay");
    VerifyStillUsable(otherHost, other, otherResult, otherWorld, 10, 4);
    std::puts("ECS First Host drained; second Host usable");

    Shutdown(otherHost, report);
    Check(WorldDestroyed(otherWorld), "最后一个 Host 关闭必须清理 World 及其所有回调");
    std::puts("ECS World callbacks cleaned after install resources");
#ifdef PANGU_FIXTURE_MODULAR
    for (const auto& completed : trace.Worlds)
    {
        Check(completed.ComponentDestroyed < trace.GameplayNativeUnload &&
                  completed.ObserverContextDestroyed < trace.GameplayNativeUnload &&
                  completed.SystemContextDestroyed < trace.GameplayNativeUnload,
              "组件及 Observer/System 捕获上下文都必须在 Gameplay 原生卸载前销毁");
    }
    std::puts("ECS World cleanup preceded Gameplay NativeUnload");
#endif
}
} // namespace
} // namespace pg::fixture::ecs

extern "C" int PanguEcsHostCheck() noexcept
{
    // flecs 的 Release 断言写 stdout，CTest 重定向时也要能读到诊断。
    pg::fixture::ecs::Check(std::setvbuf(stdout, nullptr, _IONBF, 0) == 0, "ECS 夹具无法关闭 stdout 缓冲");
    pg::fixture::ecs::Run();
    return 0;
}
