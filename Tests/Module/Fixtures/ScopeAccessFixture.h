#pragma once

#include <ScopeAccess.h>

#include <memory>

namespace pg::test
{
struct ScopeAccessFixture
{
    std::unique_ptr<int> Value = std::make_unique<int>(42);
    std::shared_ptr<module::detail::ScopeObjectControl> Control =
        std::make_shared<module::detail::ScopeObjectControl>();
    ScopeAccessFixture()
    {
        Control->Module = std::make_shared<module::detail::ScopeGate>();
        Control->Scope = std::make_shared<module::detail::ScopeGate>();
        Control->Object = Value.get();
        Control->Type = module::detail::ScopeTypeToken<int>();
        Control->Module->Open();
        Control->Scope->Open();
        Control->Gate.Open();
    }
    [[nodiscard]] module::ScopeAccess<int> Access() const
    {
        const auto result = module::detail::ScopeAccessMaker::Make<int>(Control);
        return result ? *result : module::ScopeAccess<int>{};
    }
};
} // namespace pg::test
