#pragma once

#include <ModuleError.h>
#include <Result.h>

#include <atomic>
#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <type_traits>
#include <utility>

namespace pg::module
{
template <class T>
class ScopeAccess;
template <class T>
class ScopeBorrow;

namespace detail
{
class ScopeGate final
{
public:
    [[nodiscard]] bool TryEnter() noexcept
    {
        auto current = State.load(std::memory_order_acquire);
        while ((current & kClosed) == 0 && current != kCountMask)
        {
            if (State.compare_exchange_weak(current, current + 1, std::memory_order_acquire, std::memory_order_relaxed))
            {
                return true;
            }
        }
        return false;
    }
    void Leave() noexcept
    {
        const auto previous = State.fetch_sub(1, std::memory_order_release);
        if ((previous & kCountMask) == 0)
        {
            std::terminate();
        }
    }
    void Open() noexcept
    {
        auto expected = kClosed;
        if (!State.compare_exchange_strong(expected, 0, std::memory_order_release, std::memory_order_relaxed))
        {
            std::terminate();
        }
    }
    void Close() noexcept { State.fetch_or(kClosed, std::memory_order_acq_rel); }
    [[nodiscard]] std::uint64_t Count() const noexcept { return State.load(std::memory_order_acquire) & kCountMask; }

private:
    static constexpr std::uint64_t kClosed = std::uint64_t{1} << 63U;
    static constexpr std::uint64_t kCountMask = kClosed - 1;
    std::atomic<std::uint64_t> State{kClosed};
};

template <class T>
[[nodiscard]] const void* ScopeTypeToken() noexcept
{
    // Writable storage prevents identical-constant folding. This is a same-binary
    // C++ type check, never a persistent Module/Service/Declaration identity.
    // NOLINTNEXTLINE(misc-const-correctness)
    static unsigned char token{};
    return &token;
}

struct ScopeObjectControl
{
    std::shared_ptr<ScopeGate> Module;
    std::shared_ptr<ScopeGate> Scope;
    ScopeGate Gate;
    void* Object{};
    const void* Type{};
};

struct ScopeAccessMaker
{
    template <class T>
    [[nodiscard]] static pg::Result<ScopeAccess<T>> Make(std::shared_ptr<ScopeObjectControl> control) noexcept;
};
} // namespace detail

// Owns only access protection, not the object. Release is safe on another thread.
template <class T>
class ScopeBorrow final
{
public:
    ScopeBorrow() = default;
    ~ScopeBorrow() { Release(); }
    ScopeBorrow(const ScopeBorrow&) = delete;
    ScopeBorrow& operator=(const ScopeBorrow&) = delete;
    ScopeBorrow(ScopeBorrow&& other) noexcept
        : Control(std::move(other.Control))
        , Object(std::exchange(other.Object, nullptr))
    {
    }
    ScopeBorrow& operator=(ScopeBorrow&& other) noexcept
    {
        if (this != &other)
        {
            Release();
            Control = std::move(other.Control);
            Object = std::exchange(other.Object, nullptr);
        }
        return *this;
    }
    [[nodiscard]] explicit operator bool() const noexcept { return Object != nullptr; }
    [[nodiscard]] T* Get() const noexcept { return Object; }
    [[nodiscard]] T* operator->() const noexcept { return Object; }
    [[nodiscard]] T& operator*() const noexcept { return *Object; }
    void Release() noexcept
    {
        if (Control)
        {
            Control->Gate.Leave();
            Control->Scope->Leave();
            Control->Module->Leave();
            Control.reset();
            Object = nullptr;
        }
    }

private:
    friend class ScopeAccess<T>;
    ScopeBorrow(std::shared_ptr<detail::ScopeObjectControl> control, T* object) noexcept
        : Control(std::move(control))
        , Object(object)
    {
    }
    std::shared_ptr<detail::ScopeObjectControl> Control;
    T* Object{};
};

// Copyable token; never keeps T alive by itself. Only TryBorrow grants access.
template <class T>
class ScopeAccess final
{
public:
    ScopeAccess() = default;
    [[nodiscard]] pg::Result<ScopeBorrow<T>> TryBorrow() const noexcept
    {
        const auto denied = [] { return std::unexpected(MakeModuleError(ModuleError::kNotReady)); };
        if (!Control || !Control->Module || !Control->Scope || !Control->Module->TryEnter())
        {
            return denied();
        }
        if (!Control->Scope->TryEnter())
        {
            Control->Module->Leave();
            return denied();
        }
        if (!Control->Gate.TryEnter())
        {
            Control->Scope->Leave();
            Control->Module->Leave();
            return denied();
        }
        return ScopeBorrow<T>{Control, static_cast<T*>(Control->Object)};
    }

private:
    friend struct detail::ScopeAccessMaker;
    explicit ScopeAccess(std::shared_ptr<detail::ScopeObjectControl> control) noexcept
        : Control(std::move(control))
    {
    }
    std::shared_ptr<detail::ScopeObjectControl> Control;
};

template <class T>
pg::Result<ScopeAccess<T>> detail::ScopeAccessMaker::Make(std::shared_ptr<ScopeObjectControl> control) noexcept
{
    if (!control || control->Type != ScopeTypeToken<std::remove_cv_t<T>>())
    {
        return std::unexpected(MakeModuleError(ModuleError::kTypeMismatch));
    }
    return ScopeAccess<T>{std::move(control)};
}
} // namespace pg::module
