#include "Error.h"
#include "ScopeAccess.h"
#include "ScopeIdentity.h"
#include <ModuleDeclaration.h>
#include <ModuleError.h>
#include <ModuleIdentity.h>
#include <Result.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>

#include <array>
#include <cstddef>
#include <expected>
#include <iostream>
#include <memory>
#include <optional>
#include <utility>

namespace
{
using namespace pg::module;
constexpr DeclarationId kFirst{.Bytes = {std::byte{1}}};
constexpr DeclarationId kDependency{.Bytes = {std::byte{2}}};

struct Object
{
    int* Destroyed;
    int* Started;
    int Value;
    Object(int* destroyed, int* started, int value)
        : Destroyed(destroyed)
        , Started(started)
        , Value(value)
    {
    }
    ~Object() { ++*Destroyed; }
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&) = delete;
    Object& operator=(Object&&) = delete;
};

// Adapter boundary used by the real coordinator; these tests exercise the
// production owning model, hook invocation, type checks and failure latching.
struct Sink final : detail::ScopeBuildSink
{
    std::unique_ptr<detail::ErasedScopeObject> Object;
    std::shared_ptr<detail::ScopeObjectControl> Control;
    std::optional<pg::Error> Failure;
    std::unique_ptr<detail::ErasedScopeObject> Provider;
    [[nodiscard]] ModuleId Owner() const noexcept override { return {.Bytes = {std::byte{9}}}; }
    [[nodiscard]] ScopeHandle Target() const noexcept override { return {.Domain = 1, .Slot = 2, .Generation = 1}; }
    pg::Error Reject(pg::Error error) noexcept override
    {
        if (!Failure)
        {
            Failure = error;
        }
        return *Failure;
    }
    pg::Result<std::shared_ptr<detail::ScopeObjectControl>>
    Stage(std::unique_ptr<detail::ErasedScopeObject> object) noexcept override
    {
        if (Failure)
        {
            return std::unexpected(*Failure);
        }
        if (Object)
        {
            return std::unexpected(Reject(MakeModuleError(ModuleError::kInvalidDeclaration)));
        }
        Object = std::move(object);
        Control = std::make_shared<detail::ScopeObjectControl>();
        Control->Module = std::make_shared<detail::ScopeGate>();
        Control->Scope = std::make_shared<detail::ScopeGate>();
        Control->Object = Object->Get();
        Control->Type = Object->Type();
        return Control;
    }
    pg::Result<detail::ScopeDependencyObject> Resolve(DeclarationId id) noexcept override
    {
        if (Failure)
        {
            return std::unexpected(*Failure);
        }
        if (id != kDependency || !Provider)
        {
            return std::unexpected(Reject(MakeModuleError(ModuleError::kUndeclaredDependency)));
        }
        return detail::ScopeDependencyObject{.Object = Provider->Get(), .Type = Provider->Type()};
    }
};

// ScopeHooks<Object> requires this exact mutable-reference signature.
// NOLINTNEXTLINE(misc-const-correctness)
[[nodiscard]] pg::Status Start(Object& object) noexcept
{
    ++*object.Started;
    return {};
}
void Stop(Object& /*object*/) noexcept {}
[[nodiscard]] pg::Status Poll(Object& /*object*/) noexcept { return {}; }

bool OwnershipPrecedesStartAndTokensAreInitiallyClosed()
{
    int destroyed = 0;
    int started = 0;
    ScopeAccess<Object> token;
    {
        Sink sink;
        ScopeBuildContext context(sink);
        auto object = std::make_unique<Object>(&destroyed, &started, 17);
        auto owned = context.Own(std::move(object),
                                 ScopeHooks<Object>{.Start = Start, .RequestStop = Stop, .PollStopped = Poll});
        if (!owned || object || started != 0 || destroyed != 0 || owned->TryBorrow() || context.Scope().Slot != 2)
        {
            return false;
        }
        token = *owned;
        if (!sink.Object->Start() || started != 1)
        {
            return false;
        }
        sink.Control->Module->Open();
        sink.Control->Scope->Open();
        sink.Control->Gate.Open();
        auto borrow = token.TryBorrow();
        if (!borrow || (*borrow)->Value != 17)
        {
            return false;
        }
        borrow->Release();
        sink.Control->Gate.Close();
        sink.Object.reset();
        sink.Control->Object = nullptr;
        if (destroyed != 1)
        {
            return false;
        }
    }
    return !token.TryBorrow() && destroyed == 1;
}

bool InvalidOwnershipAndTypeErrorsAreSticky()
{
    int destroyed = 0;
    int started = 0;
    Sink sink;
    ScopeBuildContext context(sink);
    const auto invalid = context.Own(std::unique_ptr<Object>());
    const auto next = context.Own(std::make_unique<Object>(&destroyed, &started, 1));
    if (invalid || next || invalid.error() != next.error() || destroyed != 1 || sink.Object)
    {
        return false;
    }
    Sink second;
    ScopeBuildContext other(second);
    const auto badHooks =
        other.Own(std::make_unique<Object>(&destroyed, &started, 2), ScopeHooks<Object>{.RequestStop = Stop});
    if (badHooks || second.Object || destroyed != 2)
    {
        return false;
    }
    Sink third;
    ScopeBuildContext dependency(third);
    third.Provider = std::make_unique<detail::ScopeObjectModel<int, std::default_delete<int>>>(std::make_unique<int>(7),
                                                                                               ScopeHooks<int>{});
    const auto wrong = dependency.Dependency<double>(kDependency);
    const auto sticky = dependency.Dependency<int>(kDependency);
    Sink missing;
    ScopeBuildContext undeclared(missing);
    const auto rejected = undeclared.Dependency<int>(kFirst);
    return !wrong && wrong.error() == MakeModuleError(ModuleError::kTypeMismatch) && !sticky &&
           sticky.error() == wrong.error() && !rejected &&
           rejected.error() == MakeModuleError(ModuleError::kUndeclaredDependency);
}

bool FactoryIsReusableAndCreatesSeparateObjects()
{
    int destroyed = 0;
    int started = 0;
    ScopeAccess<Object> first;
    ScopeAccess<Object> second;
    ScopeAccess<Object>* destination = &first;
    ScopeFactory factory(
        [config = std::make_unique<int>(33), &destination, &destroyed,
         &started](ScopeBuildContext& context) noexcept -> pg::Status
        {
            const auto object = context.Own(std::make_unique<Object>(&destroyed, &started, *config));
            if (!object)
            {
                return std::unexpected(object.error());
            }
            *destination = *object;
            return {};
        });
    Sink firstSink;
    Sink secondSink;
    ScopeBuildContext firstContext(firstSink);
    ScopeBuildContext secondContext(secondSink);
    if (!factory(firstContext))
    {
        return false;
    }
    destination = &second;
    if (!factory(secondContext))
    {
        return false;
    }
    return firstSink.Object->Get() != secondSink.Object->Get() && firstSink.Object && secondSink.Object && started == 0;
}
bool NullFunctionPointerIsAnEmptyFactory()
{
    using Function = pg::Status (*)(ScopeBuildContext&) noexcept;
    ScopeFactory const factory(static_cast<Function>(nullptr));
    return !factory;
}
} // namespace

int main()
{
    const std::array tests{
        std::pair{"null function pointer is an empty factory", NullFunctionPointerIsAnEmptyFactory},
        std::pair{"ownership precedes start and tokens initially closed",
                  OwnershipPrecedesStartAndTokensAreInitiallyClosed},
        std::pair{"invalid ownership and type errors are sticky", InvalidOwnershipAndTypeErrorsAreSticky},
        std::pair{"factory is reusable and creates separate objects", FactoryIsReusableAndCreatesSeparateObjects},
    };
    int failures = 0;
    for (const auto& [name, test] : tests)
    {
        if (!test())
        {
            std::cerr << "FAIL: " << name << '\n';
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}
