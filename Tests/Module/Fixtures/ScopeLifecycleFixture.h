#pragma once

#include <Error.h>
#include <ModuleDeclaration.h>
#include <ModuleError.h>
#include <ModuleManager.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeAccess.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>

#include <cstddef>
#include <expected>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pg::test::scope_lifecycle
{
using namespace pg::module;
using pg::service::Scope;
constexpr ModuleId kModule{.Bytes = {std::byte{1}}};
constexpr DeclarationId kObject{.Bytes = {std::byte{1}}};
constexpr pg::Error kFailure{pg::ErrorDomainId::Constant<0x73636f7065746573ULL>(), 71};

struct Export
{
    ScopeHandle Scope;
    ScopeAccess<struct Object> Access;
};
struct Counters
{
    int Registrations{};
    int Made{};
    int Destroyed{};
    int Started{};
    int Stopped{};
    int FailFactoryAt{};
    int FailStartAt{};
    bool Finished{true};
    int BusyValue{};
    std::optional<pg::Error> PollError;
    bool ProviderAlive{};
    bool BrokenOrder{};
    bool WrongDependencyType{};
    bool Reenter{};
    bool SawBusy{};
    ModuleManager* Host{};
    ModuleOperationReport* SharedReport{};
    std::string DestroyMarker;
    std::vector<Export> Exports;
    std::vector<int> DestroyOrder;
};
struct Object
{
    std::shared_ptr<Counters> State;
    int Value{};
    bool Provider{};
    bool UsesProvider{};
    Object(std::shared_ptr<Counters> state, int value, bool provider, bool usesProvider)
        : State(std::move(state))
        , Value(value)
        , Provider(provider)
        , UsesProvider(usesProvider)
    {
    }
    ~Object()
    {
        if (UsesProvider && !State->ProviderAlive)
        {
            State->BrokenOrder = true;
        }
        if (Provider)
        {
            State->ProviderAlive = false;
        }
        ++State->Destroyed;
        State->DestroyOrder.push_back(Value);
        if (!State->DestroyMarker.empty())
        {
            std::ofstream(State->DestroyMarker) << "destroyed";
        }
    }
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&) = delete;
    Object& operator=(Object&&) = delete;
};
// ScopeHooks<Object> 要求可变引用回调签名。
// NOLINTNEXTLINE(misc-const-correctness)
[[nodiscard]] inline pg::Status Start(Object& object) noexcept
{
    ++object.State->Started;
    if (object.Provider)
    {
        object.State->ProviderAlive = true;
    }
    return object.State->Started == object.State->FailStartAt ? pg::Status{std::unexpected(kFailure)} : pg::Status{};
}
// NOLINTNEXTLINE(misc-const-correctness)
inline void Stop(Object& object) noexcept { ++object.State->Stopped; }
// NOLINTNEXTLINE(misc-const-correctness)
[[nodiscard]] inline pg::Status Poll(Object& object) noexcept
{
    if (object.State->PollError)
    {
        return std::unexpected(*object.State->PollError);
    }
    return (object.State->Finished || (object.State->BusyValue != 0 && object.Value != object.State->BusyValue))
               ? pg::Status{}
               : pg::Status{std::unexpected(MakeModuleError(ModuleError::kBusy))};
}

[[nodiscard]] inline pg::Status RegisterObjects(RegistrationContext& context,
                                                const std::shared_ptr<Counters>& state) noexcept
{
    ++state->Registrations;
    return context.DeclareFactory(
        kObject, Scope::kContext,
        ScopeFactory(
            [state](ScopeBuildContext& build) noexcept -> pg::Status
            {
                const int value = ++state->Made;
                auto object = std::make_unique<Object>(state, value, false, false);
                auto access = build.Own(std::move(object),
                                        ScopeHooks<Object>{.Start = Start, .RequestStop = Stop, .PollStopped = Poll});
                if (!access)
                {
                    return std::unexpected(access.error());
                }
                state->Exports.push_back({.Scope = build.Scope(), .Access = *access});
                if (state->Reenter)
                {
                    const auto nested =
                        state->Host->CreateScope(Scope::kContext, state->Host->HostScope(), *state->SharedReport);
                    state->SawBusy = !nested && nested.error() == MakeModuleError(ModuleError::kBusy);
                }
                return value == state->FailFactoryAt ? pg::Status{std::unexpected(kFailure)} : pg::Status{};
            }),
        {}, "objects");
}
} // namespace pg::test::scope_lifecycle
