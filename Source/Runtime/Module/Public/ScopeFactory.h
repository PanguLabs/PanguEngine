#pragma once

#include <ModuleDeclaration.h>
#include <ModuleError.h>
#include <Result.h>
#include <ScopeIdentity.h>

#include <concepts>
#include <expected>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace pg::module
{
struct ScopeDependency
{
    DeclarationId Id{};
    pg::service::Scope Scope{pg::service::Scope::kHost};
};

class ScopeBuildContext;

// Owning, reusable callable. Does not require incomplete libc++ move_only_function support.
// Cold-path allocation uses the same no-exception termination policy as Module metadata.
class ScopeFactory final
{
public:
    ScopeFactory() = default;
    ~ScopeFactory() = default;
    ScopeFactory(const ScopeFactory&) = delete;
    ScopeFactory& operator=(const ScopeFactory&) = delete;
    ScopeFactory(ScopeFactory&&) noexcept = default;
    ScopeFactory& operator=(ScopeFactory&&) noexcept = default;

    template <class Factory>
        requires(!std::same_as<std::remove_cvref_t<Factory>, ScopeFactory> &&
                 std::is_nothrow_invocable_r_v<pg::Status, Factory&, ScopeBuildContext&>)
    explicit ScopeFactory(Factory factory)
    {
        if constexpr (std::is_pointer_v<Factory> || std::is_member_pointer_v<Factory>)
        {
            if (factory == nullptr)
            {
                return;
            }
        }
        Implementation = std::make_unique<Model<Factory>>(std::move(factory));
    }

    [[nodiscard]] explicit operator bool() const noexcept { return Implementation != nullptr; }
    [[nodiscard]] pg::Status operator()(ScopeBuildContext& context) noexcept
    {
        return Implementation ? Implementation->Invoke(context)
                              : pg::Status{std::unexpected(MakeModuleError(ModuleError::kInvalidDeclaration))};
    }

private:
    struct Callable
    {
        Callable() = default;
        virtual ~Callable() = default;
        Callable(const Callable&) = delete;
        Callable& operator=(const Callable&) = delete;
        Callable(Callable&&) = delete;
        Callable& operator=(Callable&&) = delete;
        [[nodiscard]] virtual pg::Status Invoke(ScopeBuildContext& context) noexcept = 0;
    };
    template <class Factory>
    struct Model final : Callable
    {
        explicit Model(Factory factory)
            : Function(std::move(factory))
        {
        }
        [[nodiscard]] pg::Status Invoke(ScopeBuildContext& context) noexcept override
        {
            return std::invoke(Function, context);
        }
        Factory Function;
    };
    std::unique_ptr<Callable> Implementation;
};
} // namespace pg::module
