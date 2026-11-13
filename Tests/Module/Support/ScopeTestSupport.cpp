#include "ScopeTestSupport.h"
#include "FailureContext.h"
#include <Check.h>

#include <Error.h>
#include <ModuleDeclaration.h>
#include <ModuleError.h>
#include <ModuleIdentity.h>
#include <Result.h>
#include <ScopeAccess.h>
#include <ScopeBuildContext.h>
#include <ScopeIdentity.h>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace pg::test
{
ProbeState::ProbeState(std::string sceneName, std::vector<int> values)
    : SceneName(std::move(sceneName))
    , Values(std::move(values))
{
}
ProbeState::~ProbeState() { Observer().Record(Event::kDestroy); }
void ScopeTestObserver::Reset() noexcept
{
    const std::scoped_lock lock(Mutex);
    History.clear();
    Token = {};
    ScopeTokens.clear();
    PrivateToken = {};
    UnloadCallback = nullptr;
    UnloadContext = nullptr;
    Options = {};
}
void ScopeTestObserver::Record(Event event) noexcept
{
    NativeUnloadCallback callback{};
    void* context{};
    {
        const std::scoped_lock lock(Mutex);
        History.push_back(event);
        if (event == Event::kNativeUnload)
        {
            callback = UnloadCallback;
            context = UnloadContext;
        }
    }
    if (callback != nullptr)
    {
        callback(context);
    }
}
void ScopeTestObserver::SetNativeUnloadCallback(NativeUnloadCallback callback, void* context) noexcept
{
    const std::scoped_lock lock(Mutex);
    UnloadCallback = callback;
    UnloadContext = context;
}
std::size_t ScopeTestObserver::RegisterCount() const noexcept
{
    const std::scoped_lock lock(Mutex);
    return static_cast<std::size_t>(std::ranges::count(History, Event::kRegister));
}
std::size_t ScopeTestObserver::NativeUnloadCount() const noexcept
{
    const std::scoped_lock lock(Mutex);
    return static_cast<std::size_t>(std::ranges::count(History, Event::kNativeUnload));
}
std::vector<Event> ScopeTestObserver::Events() const
{
    const std::scoped_lock lock(Mutex);
    return History;
}
void ScopeTestObserver::ConfigureSharedFixture(SharedFixtureOptions options) noexcept
{
    const std::scoped_lock lock(Mutex);
    Options = options;
}
SharedFixtureOptions ScopeTestObserver::SharedOptions() const noexcept
{
    const std::scoped_lock lock(Mutex);
    return Options;
}
std::size_t ScopeTestObserver::Count(Event event) const noexcept
{
    const std::scoped_lock lock(Mutex);
    return static_cast<std::size_t>(std::ranges::count(History, event));
}
void ScopeTestObserver::SetAccess(module::ScopeAccess<ProbeState> access) noexcept
{
    const std::scoped_lock lock(Mutex);
    Token = std::move(access);
}
module::ScopeAccess<ProbeState> ScopeTestObserver::Access() const noexcept
{
    const std::scoped_lock lock(Mutex);
    return Token;
}
void ScopeTestObserver::SaveAccess(module::ScopeHandle scope, module::ScopeAccess<ProbeState> access) noexcept
{
    const std::scoped_lock lock(Mutex);
    const auto found = std::ranges::find(ScopeTokens, scope, &decltype(ScopeTokens)::value_type::first);
    if (found != ScopeTokens.end())
    {
        found->second = std::move(access);
    }
    else
    {
        ScopeTokens.emplace_back(scope, std::move(access));
    }
}
module::ScopeAccess<ProbeState> ScopeTestObserver::Access(module::ScopeHandle scope) const noexcept
{
    const std::scoped_lock lock(Mutex);
    const auto found = std::ranges::find(ScopeTokens, scope, &decltype(ScopeTokens)::value_type::first);
    return found == ScopeTokens.end() ? module::ScopeAccess<ProbeState>{} : found->second;
}
void ScopeTestObserver::SavePrivateAccess(module::ScopeAccess<ProviderPrivateState> access) noexcept
{
    const std::scoped_lock lock(Mutex);
    PrivateToken = std::move(access);
}
module::ScopeAccess<ProviderPrivateState> ScopeTestObserver::PrivateAccess() const noexcept
{
    const std::scoped_lock lock(Mutex);
    return PrivateToken;
}
ScopeTestObserver& Observer() noexcept
{
    static ScopeTestObserver observer;
    return observer;
}
namespace
{
pg::Status Start(ProbeState& /*state*/, pg::FailureContext& /*failure*/) noexcept
{
    Observer().Record(Event::kStart);
    return {};
}
void Stop(ProbeState& /*state*/) noexcept { Observer().Record(Event::kStop); }
pg::Status Poll(ProbeState& /*state*/, pg::FailureContext& /*failure*/) noexcept { return {}; }
} // namespace
module::ScopeHooks<ProbeState> ProbeHooks() noexcept
{
    return {.Start = Start, .RequestStop = Stop, .PollStopped = Poll};
}
ScopeTestSink::~ScopeTestSink()
{
    if (Control)
    {
        Control->Gate.Close();
        if (Control->Gate.Count() != 0)
        {
            pg::Fatal("test scope destroyed while borrowed");
        }
        Object.reset();
        Control->Object = nullptr;
    }
}
module::ModuleId ScopeTestSink::Owner() const noexcept { return {.Bytes = {std::byte{1}}}; }
module::ScopeHandle ScopeTestSink::Target() const noexcept { return {.Domain = 1, .Slot = 1, .Generation = 1}; }
pg::Error ScopeTestSink::Reject(pg::Error error) noexcept
{
    if (!Failure)
    {
        Failure = error;
    }
    return *Failure;
}
pg::Result<std::shared_ptr<module::detail::ScopeObjectControl>>
ScopeTestSink::Stage(std::unique_ptr<module::detail::ErasedScopeObject> object) noexcept
{
    if (Failure)
    {
        return std::unexpected(*Failure);
    }
    if (Object)
    {
        return std::unexpected(Reject(module::MakeModuleError(module::ModuleError::kInvalidDeclaration)));
    }
    Object = std::move(object);
    Control = module::detail::MakeScopeObjectControl(module::detail::MakeScopeGate(), module::detail::MakeScopeGate(),
                                                     Object->Get());
    return Control;
}
pg::Result<module::detail::ScopeDependencyObject> ScopeTestSink::Resolve(module::DeclarationId /*id*/) noexcept
{
    if (Failure)
    {
        return std::unexpected(*Failure);
    }
    if (!Object)
    {
        return std::unexpected(Reject(module::MakeModuleError(module::ModuleError::kUndeclaredDependency)));
    }
    return module::detail::ScopeDependencyObject{.Object = Object->Get(), .Type = Object->Type()};
}
void ScopeTestSink::Publish() const noexcept
{
    Control->Module->Open();
    Control->Scope->Open();
    Control->Gate.Open();
}
} // namespace pg::test
