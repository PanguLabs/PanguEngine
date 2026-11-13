#include "FailureContext.h"
#include "LogWriter.h"
#include <ScopeBuildContext.h>

#include <ModuleIdentity.h>
#include <ScopeIdentity.h>
#include <utility>

namespace pg::module
{
ScopeBuildContext::ScopeBuildContext(detail::ScopeBuildSink& sink, FailureContext& failure,
                                     log::LogWriter writer) noexcept
    : Sink(&sink)
    , Diagnostics(&failure)
    , Writer(std::move(writer))
{
}
ModuleId ScopeBuildContext::Owner() const noexcept { return Sink->Owner(); }
ScopeHandle ScopeBuildContext::Scope() const noexcept { return Sink->Target(); }
} // namespace pg::module
