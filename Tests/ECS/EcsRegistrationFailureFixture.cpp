#include <PanguFlecs.h> // IWYU pragma: keep

#include "EcsHostFixture.h"
#include <EngineHost.h>
#include <HostOperation.h>
#include <World.h>
#include <WorldAccess.h>

#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>

#if defined(_WIN32) && defined(_DEBUG)
// NOLINTNEXTLINE(modernize-deprecated-headers) UCRT 在 stdlib.h 中声明该非标准接口。
#include <stdlib.h>
#endif

namespace
{
struct ProbeComponent
{
    int Value{};
};
} // namespace

int main(int argc, char* const* argv)
{
#if defined(_WIN32) && defined(_DEBUG)
    // 关闭 Debug CRT 的模态提示，保留上游 abort 的终止行为。
    static_cast<void>(_set_abort_behavior(0, _WRITE_ABORT_MSG));
#endif
    // flecs 默认写 stdout，异常终止时仍需把诊断交给监督脚本。
    if (std::setvbuf(stdout, nullptr, _IONBF, 0) != 0)
    {
        return 8;
    }
    if (argc != 2)
    {
        return 1;
    }
    const std::span arguments(argv, static_cast<std::size_t>(argc));
    const auto mode = std::string_view{arguments.back()};
    if (mode != "registered" && mode != "missing")
    {
        return 1;
    }

    pg::engine::EngineHost host;
    pg::engine::HostOperationReport report;
    if (!pg::ecs::test::StartEcsHost(host, report))
    {
        return 2;
    }
    const auto context = host.CreateContext(report);
    if (!context)
    {
        return 3;
    }
    const auto world = host.CreateWorld(*context, report);
    if (!world)
    {
        return 4;
    }
    const auto access = pg::ecs::AcquireWorld(host, *world);
    if (!access)
    {
        return 5;
    }
    const auto borrow = access->TryBorrow();
    if (!borrow)
    {
        return 6;
    }

    // NOLINTNEXTLINE(misc-include-cleaner) PanguFlecs.h 提供未标注重导出的 flecs C++ 接口。
    const pg::flecs::world& native = (*borrow)->Native();
    if (mode == "registered")
    {
        native.component<ProbeComponent>();
    }
    const auto entity = native.entity();
    std::puts("FLECS_REGISTRATION_TRIGGER_READY");
    entity.set(ProbeComponent{7});
    return entity.get<ProbeComponent>().Value == 7 ? 0 : 7;
}
