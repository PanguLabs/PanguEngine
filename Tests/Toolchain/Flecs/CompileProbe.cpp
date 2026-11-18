#if !defined(FLECS_CUSTOM_BUILD) || !defined(FLECS_CPP) || !defined(FLECS_MULTI_WORLD) || !defined(FLECS_PIPELINE) ||  \
    !defined(FLECS_LOG) || !defined(FLECS_CPP_NO_AUTO_REGISTRATION) || !defined(FLECS_KEEP_ASSERT)
#error "ThirdParty::Flecs must propagate its public configuration"
#endif

#include <flecs.h>

#if PANGU_PROBE_FLECS_STATIC != defined(flecs_STATIC)
#error "flecs_STATIC must match the library type"
#endif

#if !defined(FLECS_SYSTEM) || !defined(FLECS_FRAME) || !defined(FLECS_CACHED_QUERIES)
#error "FLECS_PIPELINE must enable its required addons"
#endif

static_assert(FLECS_VERSION_MAJOR == 4 && FLECS_VERSION_MINOR == 2 && FLECS_VERSION_PATCH == 0);

namespace
{
struct Position
{
    int Value;
};
} // namespace

int main()
{
    int observed = 0;
    const flecs::world world; // NOLINT(misc-include-cleaner) 上游统一通过 <flecs.h> 公开 API。
    world.component<Position>();
    const auto query = world.query<const Position>();
    world.system<Position>().each([](Position& position) { ++position.Value; });
    world.observer<const Position>()
        .event(flecs::OnSet) // NOLINT(misc-include-cleaner) 通过 <flecs.h> 取得内置事件。
        .each([&observed](const Position& position) { observed = position.Value; });
    world.entity().set(Position{41});
    if (!world.progress(1.0F))
    {
        return 1;
    }
    int current = 0;
    query.each([&current](const Position& position) { current = position.Value; });
    return observed == 41 && current == 42 ? 0 : 1;
}
