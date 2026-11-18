#include <World.h>

#include <EcsError.h>
#include <PanguFlecs.h>
#include <Result.h>
#include <cmath>
#include <expected>
#include <limits>

namespace pg::ecs
{
World::World() noexcept = default;
World::~World() noexcept = default;

// NOLINTNEXTLINE(misc-include-cleaner) PanguFlecs.h 提供未标注重导出的 flecs C++ 接口。
pg::flecs::world& World::Native() noexcept { return NativeWorld; }

Result<bool> World::Progress(double deltaSeconds) noexcept
{
    if (Stopping)
    {
        return std::unexpected(MakeEcsError(EcsError::kWorldStopping));
    }
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0 || deltaSeconds > std::numeric_limits<ecs_ftime_t>::max())
    {
        return std::unexpected(MakeEcsError(EcsError::kInvalidDeltaTime));
    }

    const auto delta = static_cast<ecs_ftime_t>(deltaSeconds);
    // 转换后为零会让 flecs 改用自动采样时钟。
    if (!std::isfinite(delta) || delta <= ecs_ftime_t{0})
    {
        return std::unexpected(MakeEcsError(EcsError::kInvalidDeltaTime));
    }
    return NativeWorld.progress(delta);
}
} // namespace pg::ecs
