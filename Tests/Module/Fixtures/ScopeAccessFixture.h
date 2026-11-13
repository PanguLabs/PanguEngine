#include <Check.h>
#pragma once

#include "../Support/ScopeTestSupport.h"

#include <memory>

namespace pg::test
{
struct ScopeAccessFixture
{
    ScopeTestSink Sink;
    std::shared_ptr<module::detail::ScopeObjectControl> Control;
    module::ScopeAccess<int> Token;
    ScopeAccessFixture()
    {
        pg::FailureReport failureReport;
        pg::FailureContext failureContext(failureReport);
        module::ScopeBuildContext context(Sink, failureContext);
        const auto owned = context.Own(std::make_unique<int>(42));
        if (!owned)
        {
            pg::Fatal("test fixture failed to own its object");
        }
        Token = *owned;
        Control = Sink.Control;
        Sink.Publish();
    }
    [[nodiscard]] module::ScopeAccess<int> Access() const { return Token; }
};
} // namespace pg::test
