#include "HostTestState.h"
#include <memory>
#include <utility>

namespace pg::engine::test
{
namespace
{
std::shared_ptr<HostTestState>& StateStorage()
{
    static std::shared_ptr<HostTestState> state;
    return state;
}
} // namespace
void HostTestState::Reset() noexcept { *this = {}; }
std::shared_ptr<HostTestState> CurrentHostTestState() noexcept { return StateStorage(); }
void SetHostTestState(std::shared_ptr<HostTestState> state) noexcept { StateStorage() = std::move(state); }
} // namespace pg::engine::test
