#include <ScopeAccess.h>

#include <memory>
#include <utility>

namespace pg::module::detail
{
std::shared_ptr<ScopeGate> MakeScopeGate() noexcept { return std::make_shared<ScopeGate>(); }
std::shared_ptr<ScopeObjectControl> MakeScopeObjectControl(std::shared_ptr<ScopeGate> module,
                                                           std::shared_ptr<ScopeGate> scope, void* object) noexcept
{
    auto control = std::make_shared<ScopeObjectControl>();
    control->Module = std::move(module);
    control->Scope = std::move(scope);
    control->Object = object;
    return control;
}
} // namespace pg::module::detail
