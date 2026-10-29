#pragma once

#include <ModuleDeclaration.h>
#include <ModuleError.h>
#include <ModuleIdentity.h>
#include <Result.h>
#include <ScopeAccess.h>
#include <ScopeIdentity.h>

#include <concepts>
#include <expected>
#include <memory>
#include <type_traits>
#include <utility>

namespace pg::module
{
template <class T>
struct ScopeHooks
{
    pg::Status (*Start)(T&) noexcept {};
    void (*RequestStop)(T&) noexcept {};
    pg::Status (*PollStopped)(T&) noexcept {};
};

namespace detail
{
// Resource-lifetime adapter, not a base class for business Services.
class ErasedScopeObject
{
public:
    ErasedScopeObject() = default;
    virtual ~ErasedScopeObject() = default;
    ErasedScopeObject(const ErasedScopeObject&) = delete;
    ErasedScopeObject& operator=(const ErasedScopeObject&) = delete;
    ErasedScopeObject(ErasedScopeObject&&) = delete;
    ErasedScopeObject& operator=(ErasedScopeObject&&) = delete;
    [[nodiscard]] virtual void* Get() const noexcept = 0;
    [[nodiscard]] virtual const void* Type() const noexcept = 0;
    [[nodiscard]] virtual pg::Status Start() noexcept = 0;
    virtual void RequestStop() noexcept = 0;
    [[nodiscard]] virtual pg::Status PollStopped() noexcept = 0;
};

template <class T, class Deleter>
class ScopeObjectModel final : public ErasedScopeObject
{
public:
    ScopeObjectModel(std::unique_ptr<T, Deleter> object, ScopeHooks<T> hooks)
        : Object(std::move(object))
        , Hooks(hooks)
    {
    }
    [[nodiscard]] void* Get() const noexcept override { return Object.get(); }
    [[nodiscard]] const void* Type() const noexcept override { return ScopeTypeToken<T>(); }
    [[nodiscard]] pg::Status Start() noexcept override { return Hooks.Start ? Hooks.Start(*Object) : pg::Status{}; }
    void RequestStop() noexcept override
    {
        if (Hooks.RequestStop)
        {
            Hooks.RequestStop(*Object);
        }
    }
    [[nodiscard]] pg::Status PollStopped() noexcept override
    {
        return Hooks.PollStopped ? Hooks.PollStopped(*Object) : pg::Status{};
    }

private:
    std::unique_ptr<T, Deleter> Object;
    ScopeHooks<T> Hooks;
};

struct ScopeDependencyObject
{
    void* Object{};
    const void* Type{};
};

class ScopeBuildSink
{
public:
    ScopeBuildSink() = default;
    virtual ~ScopeBuildSink() = default;
    ScopeBuildSink(const ScopeBuildSink&) = delete;
    ScopeBuildSink& operator=(const ScopeBuildSink&) = delete;
    ScopeBuildSink(ScopeBuildSink&&) = delete;
    ScopeBuildSink& operator=(ScopeBuildSink&&) = delete;
    [[nodiscard]] virtual ModuleId Owner() const noexcept = 0;
    [[nodiscard]] virtual ScopeHandle Target() const noexcept = 0;
    [[nodiscard]] virtual pg::Error Reject(pg::Error error) noexcept = 0;
    [[nodiscard]] virtual pg::Result<std::shared_ptr<ScopeObjectControl>>
    Stage(std::unique_ptr<ErasedScopeObject> object) noexcept = 0;
    [[nodiscard]] virtual pg::Result<ScopeDependencyObject> Resolve(DeclarationId id) noexcept = 0;
};
} // namespace detail

// Borrowed for one factory call. Do not retain the context or start work here.
class ScopeBuildContext final
{
public:
    explicit ScopeBuildContext(detail::ScopeBuildSink& sink) noexcept;
    ~ScopeBuildContext() = default;
    ScopeBuildContext(const ScopeBuildContext&) = delete;
    ScopeBuildContext& operator=(const ScopeBuildContext&) = delete;
    ScopeBuildContext(ScopeBuildContext&&) = delete;
    ScopeBuildContext& operator=(ScopeBuildContext&&) = delete;

    [[nodiscard]] ModuleId Owner() const noexcept;
    [[nodiscard]] ScopeHandle Scope() const noexcept;

    template <class T, class Deleter>
        requires(std::is_object_v<T> && !std::is_const_v<T> && !std::is_array_v<T> && std::is_nothrow_destructible_v<T>)
    [[nodiscard]] pg::Result<ScopeAccess<T>> Own(std::unique_ptr<T, Deleter> object, ScopeHooks<T> hooks = {})
    {
        if (!object || (hooks.RequestStop == nullptr) != (hooks.PollStopped == nullptr))
        {
            return std::unexpected(Sink->Reject(MakeModuleError(ModuleError::kInvalidDeclaration)));
        }
        auto staged = Sink->Stage(std::make_unique<detail::ScopeObjectModel<T, Deleter>>(std::move(object), hooks));
        if (!staged)
        {
            return std::unexpected(staged.error());
        }
        return detail::ScopeAccessMaker::Make<T>(std::move(*staged));
    }

    // Explicitly declared dependency, borrowed until this resource's Destroy.
    template <class T>
    [[nodiscard]] pg::Result<T*> Dependency(DeclarationId id) noexcept
    {
        const auto dependency = Sink->Resolve(id);
        if (!dependency)
        {
            return std::unexpected(dependency.error());
        }
        if (dependency->Type != detail::ScopeTypeToken<std::remove_cv_t<T>>())
        {
            return std::unexpected(Sink->Reject(MakeModuleError(ModuleError::kTypeMismatch)));
        }
        return static_cast<T*>(dependency->Object);
    }

private:
    detail::ScopeBuildSink* Sink;
};
} // namespace pg::module
