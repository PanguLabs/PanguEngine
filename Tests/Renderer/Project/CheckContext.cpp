#include <PanguApplication.h>

#include <EngineHost.h>
#include <HostOperation.h>
#include <Result.h>

#include <cstdio>
#include <expected>

namespace pg::app
{
Status RunTool(engine::EngineHost& host) noexcept
{
    engine::HostOperationReport report;
    const auto context = host.CreateContext(report);
    if (!context)
    {
        return std::unexpected(context.error());
    }
    if (const auto begun = host.BeginCloseScope(*context, report); !begun)
    {
        return begun;
    }
    if (const auto closed = host.TryFinishCloseScope(*context, report); !closed)
    {
        return closed;
    }
    std::puts("context-closed");
    return {};
}
} // namespace pg::app
