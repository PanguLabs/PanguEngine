#pragma once

#include <ModuleDeclaration.h>
#include <PanguECSExport.h>
#include <PanguFlecs.h>
#include <Result.h>
#include <cstddef>

namespace pg::ecs
{
namespace detail
{
class WorldFactory;
} // namespace detail

inline constexpr module::DeclarationId kEcsWorldDeclaration{
    .Bytes =
        {
            std::byte{0x06},
            std::byte{0x89},
            std::byte{0x55},
            std::byte{0x5c},
            std::byte{0x44},
            std::byte{0x23},
            std::byte{0x4a},
            std::byte{0x7c},
            std::byte{0x95},
            std::byte{0x28},
            std::byte{0x48},
            std::byte{0xf1},
            std::byte{0x4d},
            std::byte{0xd4},
            std::byte{0x1e},
            std::byte{0xba},
        },
};

// 在主线程使用，原生访问及 Query 生命周期受同一个 World Borrow 保护。
class PG_ECS_API World final
{
public:
    ~World() noexcept;
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;

    // 先释放原生 Query 和其他临时对象，再释放 Borrow。原生 World 仍由 Scope 拥有。
    [[nodiscard]] pg::flecs::world& Native() noexcept;
    // 成功返回 false 表示收到 quit。推进和回调期间禁止重入 Scope 或 Host 关闭。
    [[nodiscard]] Result<bool> Progress(double deltaSeconds) noexcept;

private:
    friend class detail::WorldFactory;
    World() noexcept;
    pg::flecs::world NativeWorld;
    bool Stopping{};
};
} // namespace pg::ecs
