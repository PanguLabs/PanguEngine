#pragma once

#include <Check.h>
#include <Error.h>
#include <ModuleDeclaration.h>
#include <ModuleDescriptor.h>
#include <ModuleIdentity.h>
#include <PanguFlecs.h> // IWYU pragma: keep
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <source_location>
#include <type_traits>

namespace pg::fixture::ecs
{
inline constexpr module::ModuleId kEcsGameplayId{
    .Bytes =
        {
            std::byte{0xbb},
            std::byte{0xef},
            std::byte{0x34},
            std::byte{0xff},
            std::byte{0x2e},
            std::byte{0x47},
            std::byte{0x44},
            std::byte{0x3c},
            std::byte{0xb3},
            std::byte{0x18},
            std::byte{0x60},
            std::byte{0xf8},
            std::byte{0x78},
            std::byte{0x79},
            std::byte{0x74},
            std::byte{0xf1},
        },
};
inline constexpr module::ModuleId kEcsConsumerId{
    .Bytes =
        {
            std::byte{0xe0},
            std::byte{0x83},
            std::byte{0x97},
            std::byte{0xaa},
            std::byte{0x72},
            std::byte{0x59},
            std::byte{0x41},
            std::byte{0x00},
            std::byte{0xb0},
            std::byte{0xdd},
            std::byte{0x89},
            std::byte{0x2c},
            std::byte{0x08},
            std::byte{0x9e},
            std::byte{0x44},
            std::byte{0x0b},
        },
};
inline constexpr module::DeclarationId kGameplayInstall{
    .Bytes =
        {
            std::byte{0xcb},
            std::byte{0xc4},
            std::byte{0xee},
            std::byte{0x9d},
            std::byte{0x52},
            std::byte{0x6e},
            std::byte{0x40},
            std::byte{0x8d},
            std::byte{0xae},
            std::byte{0xd0},
            std::byte{0x09},
            std::byte{0xa2},
            std::byte{0xf9},
            std::byte{0x24},
            std::byte{0x29},
            std::byte{0x6d},
        },
};
inline constexpr module::DeclarationId kConsumerInstall{
    .Bytes =
        {
            std::byte{0x52},
            std::byte{0xef},
            std::byte{0x86},
            std::byte{0xa5},
            std::byte{0x89},
            std::byte{0x10},
            std::byte{0x43},
            std::byte{0xae},
            std::byte{0xa1},
            std::byte{0x54},
            std::byte{0xe9},
            std::byte{0xe7},
            std::byte{0x21},
            std::byte{0xe7},
            std::byte{0x89},
            std::byte{0xe4},
        },
};
inline constexpr module::ModuleId kTraceModuleId{.Bytes = {std::byte{0xec}, std::byte{0x04}}};
inline constexpr module::DeclarationId kTraceResource{.Bytes = {std::byte{0xec}, std::byte{0x04}}};
inline constexpr Error kInjectedFailure{ErrorDomainId::Constant<0x6563736669787475>(), 1};

[[nodiscard]] const module::ModuleDefinition* QueryEcsGameplay() noexcept;
[[nodiscard]] const module::ModuleDefinition* QueryEcsConsumer() noexcept;

inline constexpr auto kPositionName = "EcsFixturePosition";
inline constexpr auto kStateEntity = "EcsFixtureState";

inline void Check(bool condition, const char* message,
                  std::source_location location = std::source_location::current()) noexcept
{
    if (!condition)
    {
        // Fatal 不经普通输出；CTest 仍需能读到具体失败条件。
        std::fputs(message, stderr);
        std::fputc('\n', stderr);
        pg::Fatal(message, {}, location);
    }
}

struct WorldTrace
{
    std::size_t InstallDestroyed{};
    std::size_t ComponentDestroyed{};
    std::size_t ObserverContextDestroyed{};
    std::size_t SystemContextDestroyed{};
    int ObserverCalls{};
    int SystemCalls{};
};

struct EcsFixtureTrace
{
    std::array<WorldTrace, 4> Worlds{};
    std::size_t WorldCount{};
    std::size_t Sequence{};
    std::size_t GameplayNativeUnload{};
    bool FailNextWorld{};
};

// POSIX 按共享类型的 RTTI 值匹配；Windows 保持类型在各镜像内定义。
#ifdef _WIN32
struct TraceResource
#else
struct __attribute__((visibility("default"))) TraceResource
#endif
{
    EcsFixtureTrace* Trace;
};

struct WorldEvents
{
    EcsFixtureTrace* Trace;
    WorldTrace* World;

    void Record(std::size_t WorldTrace::* event) const noexcept { World->*event = ++Trace->Sequence; }
};

// 仅 Gameplay 安装的哨兵组件携带这个状态，移动组件时保持唯一所有权。
struct ComponentLifetime
{
    explicit ComponentLifetime(WorldEvents events) noexcept
        : Events(events)
    {
    }
    ~ComponentLifetime() { Events.Record(&WorldTrace::ComponentDestroyed); }
    ComponentLifetime(const ComponentLifetime&) = delete;
    ComponentLifetime& operator=(const ComponentLifetime&) = delete;
    ComponentLifetime(ComponentLifetime&&) = delete;
    ComponentLifetime& operator=(ComponentLifetime&&) = delete;

    WorldEvents Events;
};

struct Position
{
    double Value{};
    std::unique_ptr<ComponentLifetime> Lifetime;
};

struct Velocity
{
    double Value{};
};

struct Tracks
{
};

enum class MotionState : std::uint8_t
{
    kIdle,
    kMoving,
};

inline void BindMotionState(const pg::flecs::world& world)
{
    // 关闭自动登记的 flecs 需要在派生枚举实体前显式绑定底层类型。
    world.component<std::underlying_type_t<MotionState>>();
    world.component<MotionState>();
}

#ifdef _WIN32
struct GameplayInstall
#else
struct __attribute__((visibility("default"))) GameplayInstall
#endif
{
    explicit GameplayInstall(WorldEvents events) noexcept
        : Events(events)
    {
    }
    ~GameplayInstall() { Events.Record(&WorldTrace::InstallDestroyed); }
    GameplayInstall(const GameplayInstall&) = delete;
    GameplayInstall& operator=(const GameplayInstall&) = delete;
    GameplayInstall(GameplayInstall&&) = delete;
    GameplayInstall& operator=(GameplayInstall&&) = delete;

    WorldEvents Events;
};

struct Observation
{
    pg::flecs::entity_t ProviderId{};
    pg::flecs::entity_t ConsumerBefore{};
    pg::flecs::entity_t ConsumerId{};
    pg::flecs::entity_t IdleConstant{};
    pg::flecs::entity_t MovingConstant{};
    pg::flecs::entity_t ConsumerIdleConstant{};
    pg::flecs::entity_t ConsumerMovingConstant{};
    ecs_xtor_t OwnerDtorBefore{};
    ecs_xtor_t OwnerDtorAfter{};
    bool ConsumerSawIdle{};
};
} // namespace pg::fixture::ecs
