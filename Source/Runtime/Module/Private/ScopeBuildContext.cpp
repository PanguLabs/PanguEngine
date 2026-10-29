#include <ScopeBuildContext.h>

#include <ModuleIdentity.h>
#include <ScopeIdentity.h>

namespace pg::module
{
ScopeBuildContext::ScopeBuildContext(detail::ScopeBuildSink& sink) noexcept
    : Sink(&sink)
{
}
ModuleId ScopeBuildContext::Owner() const noexcept { return Sink->Owner(); }
ScopeHandle ScopeBuildContext::Scope() const noexcept { return Sink->Target(); }
} // namespace pg::module
