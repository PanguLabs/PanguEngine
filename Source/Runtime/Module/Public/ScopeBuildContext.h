#pragma once

#include <PanguModuleExport.h>

#include <FailureContext.h>
#include <LogWriter.h>
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
#include <typeinfo>
#include <utility>

namespace pg::module
{
template <class T>
struct ScopeHooks
{
    pg::Status (*Start)(T&, FailureContext&) noexcept {};
    void (*RequestStop)(T&) noexcept {};
    pg::Status (*PollStopped)(T&, FailureContext&) noexcept {};
};

namespace detail
{
// Resource-lifetime adapter, not a base class for business Services.
class PG_MODULE_API ErasedScopeObject
{
public:
    ErasedScopeObject() = default;
    virtual ~ErasedScopeObject() = default;
    ErasedScopeObject(const ErasedScopeObject&) = delete;
    ErasedScopeObject& operator=(const ErasedScopeObject&) = delete;
    ErasedScopeObject(ErasedScopeObject&&) = delete;
    ErasedScopeObject& operator=(ErasedScopeObject&&) = delete;
    [[nodiscard]] virtual void* Get() const noexcept = 0;
    [[nodiscard]] virtual const std::type_info& Type() const noexcept = 0;
    [[nodiscard]] virtual pg::Status Start(FailureContext& failure) noexcept = 0;
    virtual void RequestStop() noexcept = 0;
    [[nodiscard]] virtual pg::Status PollStopped(FailureContext& failure) noexcept = 0;
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
    [[nodiscard]] const std::type_info& Type() const noexcept override { return typeid(T); }
    [[nodiscard]] pg::Status Start(FailureContext& failure) noexcept override
    {
        return Hooks.Start ? Hooks.Start(*Object, failure) : pg::Status{};
    }
    void RequestStop() noexcept override
    {
        if (Hooks.RequestStop)
        {
            Hooks.RequestStop(*Object);
        }
    }
    [[nodiscard]] pg::Status PollStopped(FailureContext& failure) noexcept override
    {
        return Hooks.PollStopped ? Hooks.PollStopped(*Object, failure) : pg::Status{};
    }

private:
    std::unique_ptr<T, Deleter> Object;
    ScopeHooks<T> Hooks;
};

struct ScopeDependencyObject
{
    void* Object{};
    // 仅在资源及其实现代码受保护期间借用，禁止保存在访问控制块中。
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    const std::type_info& Type;
};

class PG_MODULE_API ScopeBuildSink
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
class PG_MODULE_API ScopeBuildContext final
{
public:
    ScopeBuildContext(detail::ScopeBuildSink& sink, FailureContext& failure, log::LogWriter writer = {}) noexcept;
    ~ScopeBuildContext() = default;
    ScopeBuildContext(const ScopeBuildContext&) = delete;
    ScopeBuildContext& operator=(const ScopeBuildContext&) = delete;
    ScopeBuildContext(ScopeBuildContext&&) = delete;
    ScopeBuildContext& operator=(ScopeBuildContext&&) = delete;

    [[nodiscard]] ModuleId Owner() const noexcept;
    [[nodiscard]] ScopeHandle Scope() const noexcept;

    [[nodiscard]] log::LogWriter Log() const noexcept { return Writer; }
    [[nodiscard]] FailureContext& Failure() noexcept { return *Diagnostics; }

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
        // T 来自刚刚接纳的拥有模型，不允许调用方拼接 void* 与任意类型。
        return ScopeAccess<T>{std::move(*staged)};
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
        if (dependency->Type != typeid(T))
        {
            return std::unexpected(Sink->Reject(MakeModuleError(ModuleError::kTypeMismatch)));
        }
        return static_cast<T*>(dependency->Object);
    }

private:
    detail::ScopeBuildSink* Sink;
    FailureContext* Diagnostics;
    log::LogWriter Writer;
};
} // namespace pg::module
