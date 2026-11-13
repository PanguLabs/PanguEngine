#pragma once

#include <Check.h>
#include <Log.h>
#include <ModuleBuiltin.h>
#include <ModuleError.h>
#include <ModuleManager.h>
#include <RegistrationContext.h>
#include <ScopeBuildContext.h>

#include <memory>
#include <utility>
#include <vector>

namespace pg::test::module_diagnostics
{
inline constexpr module::ModuleId kOwner{.Bytes = {std::byte{0x9d}}};
inline constexpr Error kOriginal{ErrorDomainId::Constant<0x7461736b39000001ULL>(), 101};
inline constexpr Error kCleanup{ErrorDomainId::Constant<0x7461736b39000001ULL>(), 102};
inline constexpr Error kMismatch{ErrorDomainId::Constant<0x7461736b39000001ULL>(), 103};
struct Controls;
struct Object
{
    Controls* State{};
    log::LogWriter Writer;
    unsigned Index{};
    unsigned Serial{};
};
struct Controls
{
    inline static Controls* gCurrent{};
    bool FailRegister{};
    bool FailFactory{};
    bool FailStart{};
    int FailStartAt{-1};
    bool Mismatch{};
    bool PollError{};
    bool PollUsesOriginalError{};
    int FailPollAt{-1};
    bool Done{true};
    bool Reenter{};
    bool Emit{};
    unsigned ResourceCount{1};
    std::byte DeclarationPrefix{0x9e};
    service::Scope Kind{service::Scope::kHost};
    unsigned Created{};
    unsigned ReadyThrough{};
    unsigned OriginLine{};
    module::ModuleManager* Manager{};
    module::ModuleOperationReport* Destination{};
    std::vector<module::ScopeAccess<Object>> Access;
    std::vector<log::SubmitResult> Submissions;
    Controls() { gCurrent = this; }
    ~Controls() { gCurrent = nullptr; }
    Controls(const Controls&) = delete;
    Controls& operator=(const Controls&) = delete;
    Controls(Controls&&) = delete;
    Controls& operator=(Controls&&) = delete;
};
inline void Capture(Controls& state, FailureContext& failure)
{
    const auto location = std::source_location::current();
    state.OriginLine = location.line();
    failure.SetPrimary(state.Mismatch ? kMismatch : kOriginal, "owned callback origin", location);
    if (state.Reenter)
    {
        const auto busy = state.Manager->BeginShutdown(*state.Destination);
        PG_CHECK(!busy && busy.error() == module::MakeModuleError(module::ModuleError::kBusy),
                 "expected reentry rejection");
    }
    failure.AddContext("callback", "alive after reentry");
}
inline Status Start(Object& object, FailureContext& failure) noexcept
{
    if (object.State->Emit)
    {
        object.State->Submissions.push_back(PG_LOG_INFO(object.Writer, "module", "start"));
    }
    if (object.State->FailStart &&
        (object.State->FailStartAt < 0 || std::cmp_equal(object.State->FailStartAt, object.Index)))
    {
        Capture(*object.State, failure);
        return std::unexpected(kOriginal);
    }
    return {};
}
inline void Stop(Object& /*unused*/) noexcept {}
// ScopeHooks 的固定函数指针签名要求可变对象引用。
// NOLINTNEXTLINE(misc-const-correctness)
inline Status Poll(Object& object, FailureContext& failure) noexcept
{
    if (object.State->Done || object.Serial <= object.State->ReadyThrough)
    {
        return {};
    }
    if (object.State->PollError &&
        (object.State->FailPollAt < 0 || std::cmp_equal(object.State->FailPollAt, object.Index)))
    {
        const auto error = object.State->PollUsesOriginalError ? kOriginal : kCleanup;
        failure.SetPrimary(error, "poll stopped failure");
        return std::unexpected(error);
    }
    return std::unexpected(module::MakeModuleError(module::ModuleError::kBusy));
}
inline Status RegisterControls(module::RegistrationContext& context, Controls& state) noexcept
{
    if (state.Emit)
    {
        state.Submissions.push_back(PG_LOG_INFO(context.Log(), "module", "register"));
    }
    if (state.FailRegister)
    {
        Capture(state, context.Failure());
        return std::unexpected(kOriginal);
    }
    for (unsigned index = 0; index < state.ResourceCount; ++index)
    {
        const module::DeclarationId id{.Bytes = {state.DeclarationPrefix, static_cast<std::byte>(index)}};
        const auto declared = context.DeclareFactory(
            id, state.Kind,
            module::ScopeFactory(
                [&state, index](module::ScopeBuildContext& build) noexcept -> Status
                {
                    if (state.Emit)
                    {
                        state.Submissions.push_back(PG_LOG_INFO(build.Log(), "module", "factory"));
                    }
                    if (state.FailFactory)
                    {
                        Capture(state, build.Failure());
                        return std::unexpected(kOriginal);
                    }
                    const auto access = build.Own(
                        std::make_unique<Object>(
                            Object{.State = &state, .Writer = build.Log(), .Index = index, .Serial = ++state.Created}),
                        module::ScopeHooks<Object>{.Start = Start, .RequestStop = Stop, .PollStopped = Poll});
                    if (!access)
                    {
                        return std::unexpected(access.error());
                    }
                    state.Access.push_back(*access);
                    return {};
                }));
        if (!declared)
        {
            return declared;
        }
    }
    return {};
}
inline Status Register(module::RegistrationContext& context) noexcept
{
    return RegisterControls(context, *Controls::gCurrent);
}
inline const module::ModuleDefinition* Query() noexcept
{
    static const module::ModuleDefinition kDefinition{
        .Descriptor = {.Id = kOwner, .Dependencies = {}, .DisplayName = "diagnostics fixture", .BuildIdentity = {}},
        .RegisterModule = Register};
    return &kDefinition;
}
inline Result<module::ModuleManager> Create(module::RegistrationScope& declarations,
                                            module::ModuleOperationReport& report)
{
    std::vector<module::QueryModuleFn> queries(module::BuiltinResidentQueries().begin(),
                                               module::BuiltinResidentQueries().end());
    queries.push_back(Query);
    return module::ModuleManager::Create(declarations, queries, {}, report);
}
} // namespace pg::test::module_diagnostics
