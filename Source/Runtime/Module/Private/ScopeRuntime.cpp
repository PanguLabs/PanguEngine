#include "ScopeRuntime.h"
#include "Error.h"

#include <ModuleDeclaration.h>
#include <ModuleError.h>
#include <ModuleIdentity.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeAccess.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>
#include <StaticModuleHost.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace pg::module::detail
{
namespace
{
using Kind = pg::service::Scope;
constexpr std::size_t kMaxScopes = 256;

void Snapshot(ModuleOperationReport& report, ModuleOperationProgress progress, ModuleId module = ModuleId(),
              ScopeHandle scope = ScopeHandle(), std::optional<pg::Error> error = std::nullopt,
              std::optional<pg::Error> original = std::nullopt, DeclarationId declaration = DeclarationId())
{
    report = {
        .Progress = progress,
        .PrimaryError = original ? original : error,
        .Module = module,
        .RelatedModule = ModuleId(),
        .Scope = scope,
        .Declaration = declaration,
        .OriginalError = original,
        .CleanupError = original ? error : std::nullopt,
    };
}
[[nodiscard]] pg::Status Fail(ModuleOperationReport& report, ModuleError code, ModuleId module = ModuleId(),
                              ScopeHandle scope = ScopeHandle(), ModuleId related = ModuleId())
{
    const auto error = MakeModuleError(code);
    Snapshot(report, ModuleOperationProgress::kUnchanged, module, scope, error);
    report.RelatedModule = related;
    return std::unexpected(error);
}
} // namespace

pg::Result<std::uint64_t> AllocateScopeDomain() noexcept
{
    static std::atomic<std::uint64_t> next{1};
    auto current = next.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<std::uint64_t>::max())
    {
        if (next.compare_exchange_weak(current, current + 1, std::memory_order_relaxed))
        {
            return current;
        }
    }
    return std::unexpected(MakeModuleError(ModuleError::kLimitExceeded));
}

struct ScopeRuntime::State
{
    using Recipe = RegistrationScope::Recipe;
    struct Module
    {
        StaticModuleRecord* Record{};
        std::shared_ptr<ScopeGate> Gate = std::make_shared<ScopeGate>();
        std::vector<ModuleId> ServiceDependencies;
        std::optional<pg::Error> Original;
    };
    struct Resource
    {
        ModuleId Owner{};
        DeclarationId Id{};
        std::uint64_t Serial{};
        std::unique_ptr<ErasedScopeObject> Object;
        std::shared_ptr<ScopeObjectControl> Control;
        std::vector<Resource*> Dependencies;
        bool Ready{};
        bool StartAttempted{};
        bool Closing{};
        bool StopRequested{};
        bool Stopped{};
        bool Destroyed{};
        std::optional<pg::Error> Error;
    };
    struct Scope
    {
        ScopeHandle Handle{};
        Kind Type{};
        ScopeHandle Parent{};
        std::shared_ptr<ScopeGate> Gate = std::make_shared<ScopeGate>();
        std::vector<std::unique_ptr<Resource>> Resources;
        bool Ready{};
        bool Closing{};
        bool Delivered{};
        std::optional<pg::Error> Original;
    };
    struct Slot
    {
        std::uint32_t Generation{1};
        std::unique_ptr<Scope> Owner;
        bool Exhausted{};
    };
    struct Operation
    {
        State* Owner;
        bool Accepted;
        explicit Operation(State& owner)
            : Owner(&owner)
            , Accepted(!owner.Operating)
        {
            if (Accepted)
            {
                Owner->Operating = true;
            }
        }
        ~Operation()
        {
            if (Accepted)
            {
                Owner->Operating = false;
            }
        }
        Operation(const Operation&) = delete;
        Operation& operator=(const Operation&) = delete;
        Operation(Operation&&) = delete;
        Operation& operator=(Operation&&) = delete;
    };

    std::uint64_t Domain;
    RegistrationScope* Declarations;
    std::vector<Module> Modules;
    std::vector<Slot> Slots;
    std::uint64_t NextResource{1};
    bool Operating{};
    bool ShuttingDown{};
    std::optional<pg::Error> ShutdownOriginal;
    DeclarationId BuildDeclaration{};

    State(std::uint64_t domain, RegistrationScope& declarations, std::span<StaticModuleRecord> records)
        : Domain(domain)
        , Declarations(&declarations)
    {
        for (auto& record : records)
        {
            Module module;
            module.Record = &record;
            Modules.push_back(std::move(module));
        }
        Slot root;
        root.Owner = std::make_unique<Scope>();
        root.Owner->Handle = {.Domain = domain, .Slot = 1, .Generation = 1};
        root.Owner->Type = Kind::kHost;
        root.Owner->Ready = true;
        root.Owner->Delivered = true;
        root.Owner->Gate->Open();
        Slots.push_back(std::move(root));
    }
    Module* Find(ModuleId id)
    {
        const auto found =
            std::ranges::find(Modules, id, [](const auto& module) { return module.Record->Descriptor.Id; });
        return found == Modules.end() ? nullptr : &*found;
    }
    Scope* Find(ScopeHandle handle)
    {
        if (handle.Domain != Domain || handle.Slot == 0 || handle.Slot > Slots.size())
        {
            return nullptr;
        }
        auto const& slot = Slots.at(handle.Slot - 1);
        return slot.Owner && slot.Generation == handle.Generation ? slot.Owner.get() : nullptr;
    }
    Recipe* Definition(DeclarationId id)
    {
        for (const auto& recipe : Declarations->Recipes)
        {
            if (recipe->HostDomain == Domain && recipe->Id == id)
            {
                return recipe.get();
            }
        }
        return nullptr;
    }
    static Resource* FindResource(Scope& scope, DeclarationId id)
    {
        const auto found = std::ranges::find_if(scope.Resources, [&](const auto& value)
                                                { return value->Id == id && !value->Destroyed; });
        return found == scope.Resources.end() ? nullptr : found->get();
    }
    Scope* Ancestor(Scope& scope, Kind kind)
    {
        auto* current = &scope;
        while ((current != nullptr) && current->Type != kind)
        {
            current = Find(current->Parent);
        }
        return current;
    }
    bool Descendant(Scope& scope, ScopeHandle parent)
    {
        auto const* current = &scope;
        while (current != nullptr)
        {
            if (current->Handle == parent)
            {
                return true;
            }
            current = Find(current->Parent);
        }
        return false;
    }
    bool HasChildren(ScopeHandle handle)
    {
        return std::ranges::any_of(Slots, [&](const auto& slot) { return slot.Owner && slot.Owner->Parent == handle; });
    }

    static std::vector<ModuleId> Dependencies(Module& module)
    {
        auto result = module.ServiceDependencies;
        for (const auto& binding : module.Record->Bindings)
        {
            if (binding.IsBound() && std::ranges::find(result, binding.Dependency) == result.end())
            {
                result.push_back(binding.Dependency);
            }
        }
        return result;
    }
    [[nodiscard]] pg::Result<std::vector<ModuleId>> ModuleOrder(bool prospective, ModuleId activating = ModuleId())
    {
        std::vector<std::vector<ModuleId>> edges;
        edges.reserve(Modules.size());
        for (auto& module : Modules)
        {
            edges.push_back(Dependencies(module));
        }
        if (prospective)
        {
            std::vector<ModuleId> pending{activating};
            std::vector<ModuleId> visited;
            while (!pending.empty())
            {
                const auto id = pending.back();
                pending.pop_back();
                if (std::ranges::find(visited, id) != visited.end())
                {
                    continue;
                }
                visited.push_back(id);
                auto* owner = Find(id);
                if ((owner == nullptr) || owner->Record->State == StaticModuleState::kUnloaded)
                {
                    continue;
                }
                const auto index = static_cast<std::size_t>(owner - Modules.data());
                for (const auto dependency : Dependencies(*owner))
                {
                    pending.push_back(dependency);
                }
                for (const auto& recipe : Declarations->Recipes)
                {
                    if (recipe->HostDomain != Domain || recipe->Owner != id)
                    {
                        continue;
                    }
                    for (const auto& dependency : recipe->Dependencies)
                    {
                        const auto* provider = Definition(dependency.Id);
                        if ((provider != nullptr) && provider->Owner != id)
                        {
                            if (std::ranges::find(edges.at(index), provider->Owner) == edges.at(index).end())
                            {
                                edges.at(index).push_back(provider->Owner);
                            }
                            pending.push_back(provider->Owner);
                        }
                    }
                }
            }
        }
        std::vector<ModuleId> result;
        while (result.size() != Modules.size())
        {
            const auto before = result.size();
            for (std::size_t index = 0; index < Modules.size(); ++index)
            {
                const auto id = Modules.at(index).Record->Descriptor.Id;
                if (std::ranges::find(result, id) != result.end())
                {
                    continue;
                }
                if (std::ranges::all_of(edges.at(index), [&](ModuleId dependency)
                                        { return std::ranges::find(result, dependency) != result.end(); }))
                {
                    result.push_back(id);
                }
            }
            if (before == result.size())
            {
                return std::unexpected(MakeModuleError(ModuleError::kDependencyCycle));
            }
        }
        return result;
    }
    bool InUse(ModuleId id)
    {
        return std::ranges::any_of(Modules,
                                   [&](auto& module)
                                   {
                                       if (module.Record->Descriptor.Id == id ||
                                           module.Record->State == StaticModuleState::kUnloaded)
                                       {
                                           return false;
                                       }
                                       const auto dependencies = Dependencies(module);
                                       return std::ranges::find(dependencies, id) != dependencies.end();
                                   });
    }
    [[nodiscard]] pg::Result<std::vector<ModuleId>> ResolveDependencies(Module& module)
    {
        std::vector<ModuleId> result;
        for (auto const dependency : Dependencies(module))
        {
            const auto* provider = Find(dependency);
            if ((provider == nullptr) || provider->Record->State != StaticModuleState::kActive)
            {
                return std::unexpected(MakeModuleError(ModuleError::kDependencyNotActive));
            }
        }
        for (const auto& recipe : Declarations->Recipes)
        {
            if (recipe->HostDomain != Domain || recipe->Owner != module.Record->Descriptor.Id)
            {
                continue;
            }
            for (const auto& dependency : recipe->Dependencies)
            {
                const auto* provider = Definition(dependency.Id);
                if (provider == nullptr)
                {
                    return std::unexpected(MakeModuleError(ModuleError::kMissingDependency));
                }
                if (provider->Kind != dependency.Scope)
                {
                    return std::unexpected(MakeModuleError(ModuleError::kInvalidScope));
                }
                if (provider->Owner != recipe->Owner)
                {
                    const auto* owner = Find(provider->Owner);
                    if ((owner == nullptr) || owner->Record->State != StaticModuleState::kActive)
                    {
                        return std::unexpected(MakeModuleError(ModuleError::kDependencyNotActive));
                    }
                    if (std::ranges::find(result, provider->Owner) == result.end())
                    {
                        result.push_back(provider->Owner);
                    }
                }
            }
        }
        return result;
    }

    struct BuildFrame final : ScopeBuildSink
    {
        State* Runtime;
        Scope* TargetScope;
        Recipe* Definition;
        std::vector<Resource*> Providers;
        Resource* Owned{};
        std::optional<pg::Error> Failure;
        BuildFrame(State& runtime, Scope& scope, Recipe& definition, std::vector<Resource*> providers)
            : Runtime(&runtime)
            , TargetScope(&scope)
            , Definition(&definition)
            , Providers(std::move(providers))
        {
        }
        [[nodiscard]] ModuleId Owner() const noexcept override { return Definition->Owner; }
        [[nodiscard]] ScopeHandle Target() const noexcept override { return TargetScope->Handle; }
        [[nodiscard]] pg::Error Reject(pg::Error error) noexcept override
        {
            if (!Failure)
            {
                Failure = error;
            }
            return *Failure;
        }
        [[nodiscard]] pg::Result<std::shared_ptr<ScopeObjectControl>>
        Stage(std::unique_ptr<ErasedScopeObject> object) noexcept override
        {
            if (Failure)
            {
                return std::unexpected(*Failure);
            }
            if ((Owned != nullptr) || !object || (object->Get() == nullptr))
            {
                return std::unexpected(Reject(MakeModuleError(ModuleError::kInvalidDeclaration)));
            }
            const auto alive = std::ranges::count_if(TargetScope->Resources,
                                                     [](const auto& resource) { return !resource->Destroyed; });
            if (std::cmp_greater_equal(alive, kMaxScopeDeclarations) ||
                Runtime->NextResource == std::numeric_limits<std::uint64_t>::max())
            {
                return std::unexpected(Reject(MakeModuleError(ModuleError::kLimitExceeded)));
            }
            auto resource = std::make_unique<Resource>();
            resource->Owner = Definition->Owner;
            resource->Id = Definition->Id;
            resource->Serial = Runtime->NextResource++;
            resource->Object = std::move(object);
            resource->Dependencies = Providers;
            resource->Control = std::make_shared<ScopeObjectControl>();
            resource->Control->Module = Runtime->Find(Definition->Owner)->Gate;
            resource->Control->Scope = TargetScope->Gate;
            resource->Control->Object = resource->Object->Get();
            resource->Control->Type = resource->Object->Type();
            Owned = resource.get();
            auto control = resource->Control;
            TargetScope->Resources.push_back(std::move(resource));
            return control;
        }
        [[nodiscard]] pg::Result<ScopeDependencyObject> Resolve(DeclarationId id) noexcept override
        {
            if (Failure)
            {
                return std::unexpected(*Failure);
            }
            const auto found = std::ranges::find(Providers, id, [](const auto* value) { return value->Id; });
            if (found == Providers.end())
            {
                return std::unexpected(Reject(MakeModuleError(ModuleError::kUndeclaredDependency)));
            }
            if ((*found)->Closing || !(*found)->Ready || !(*found)->Object)
            {
                return std::unexpected(Reject(MakeModuleError(ModuleError::kNotReady)));
            }
            return ScopeDependencyObject{.Object = (*found)->Object->Get(), .Type = (*found)->Object->Type()};
        }
    };

    [[nodiscard]] pg::Result<std::vector<Recipe*>> RecipeOrder(ModuleId owner, Kind kind)
    {
        std::vector<Recipe*> pending;
        for (const auto& recipe : Declarations->Recipes)
        {
            if (recipe->HostDomain == Domain && recipe->Owner == owner && recipe->Kind == kind)
            {
                pending.push_back(recipe.get());
            }
        }
        std::vector<Recipe*> ordered;
        while (ordered.size() != pending.size())
        {
            const auto before = ordered.size();
            for (auto* recipe : pending)
            {
                if (std::ranges::find(ordered, recipe) != ordered.end())
                {
                    continue;
                }
                const bool ready = std::ranges::all_of(recipe->Dependencies,
                                                       [&](const auto& dependency)
                                                       {
                                                           const auto found = std::ranges::find(pending, dependency.Id,
                                                                                                [](const auto* item)
                                                                                                { return item->Id; });
                                                           return dependency.Scope != kind || found == pending.end() ||
                                                                  std::ranges::find(ordered, *found) != ordered.end();
                                                       });
                if (ready)
                {
                    ordered.push_back(recipe);
                }
            }
            if (before == ordered.size())
            {
                return std::unexpected(MakeModuleError(ModuleError::kDependencyCycle));
            }
        }
        return ordered;
    }

    [[nodiscard]] pg::Status Build(ModuleId owner, Scope& scope)
    {
        const auto ordered = RecipeOrder(owner, scope.Type);
        if (!ordered)
        {
            return std::unexpected(ordered.error());
        }
        for (auto* recipe : *ordered)
        {
            BuildDeclaration = recipe->Id;
            std::vector<Resource*> providers;
            for (const auto& dependency : recipe->Dependencies)
            {
                auto* providerScope = Ancestor(scope, dependency.Scope);
                auto* provider = (providerScope != nullptr) ? FindResource(*providerScope, dependency.Id) : nullptr;
                if ((provider == nullptr) || !provider->Ready || provider->Closing)
                {
                    return std::unexpected(MakeModuleError(ModuleError::kNotReady));
                }
                providers.push_back(provider);
            }
            BuildFrame frame(*this, scope, *recipe, std::move(providers));
            ScopeBuildContext context(frame);
            const auto result = recipe->Factory(context);
            if (frame.Failure || !result)
            {
                return std::unexpected(frame.Failure ? *frame.Failure : result.error());
            }
            if (frame.Owned == nullptr)
            {
                return std::unexpected(MakeModuleError(ModuleError::kInvalidDeclaration));
            }
            frame.Owned->StartAttempted = true;
            if (const auto started = frame.Owned->Object->Start(); !started)
            {
                return started;
            }
            frame.Owned->Ready = true;
        }
        return {};
    }

    static void Close(Resource& resource)
    {
        resource.Closing = true;
        resource.Control->Gate.Close();
    }
    void CloseScope(Scope& scope)
    {
        const auto root = scope.Handle;
        for (auto const& slot : Slots)
        {
            if (slot.Owner && Descendant(*slot.Owner, root))
            {
                slot.Owner->Closing = true;
                slot.Owner->Gate->Close();
                for (auto const& resource : slot.Owner->Resources)
                {
                    if (!resource->Destroyed)
                    {
                        Close(*resource);
                    }
                }
            }
        }
    }
    void CloseModule(Module& module)
    {
        module.Gate->Close();
        module.Record->State = StaticModuleState::kCleanupPending;
        for (auto const& slot : Slots)
        {
            if (slot.Owner)
            {
                for (auto const& resource : slot.Owner->Resources)
                {
                    if (resource->Owner == module.Record->Descriptor.Id && !resource->Destroyed)
                    {
                        Close(*resource);
                    }
                }
            }
        }
    }
    [[nodiscard]] bool ModuleDependsOn(ModuleId owner, ModuleId provider)
    {
        std::vector<ModuleId> pending{owner};
        std::vector<ModuleId> visited;
        while (!pending.empty())
        {
            const auto current = pending.back();
            pending.pop_back();
            if (std::ranges::find(visited, current) != visited.end())
            {
                continue;
            }
            visited.push_back(current);
            if (auto* module = Find(current))
            {
                for (const auto dependency : Dependencies(*module))
                {
                    if (dependency == provider)
                    {
                        return true;
                    }
                    pending.push_back(dependency);
                }
            }
        }
        return false;
    }
    bool HasDependent(Resource& provider, ScopeHandle providerScope)
    {
        for (const auto& slot : Slots)
        {
            if (slot.Owner)
            {
                for (const auto& resource : slot.Owner->Resources)
                {
                    if (!resource->Destroyed &&
                        (std::ranges::find(resource->Dependencies, &provider) != resource->Dependencies.end() ||
                         (slot.Owner->Handle == providerScope && resource->Owner != provider.Owner &&
                          ModuleDependsOn(resource->Owner, provider.Owner))))
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }
    bool RetireScopes()
    {
        bool changed = false;
        for (auto& slot : Slots)
        {
            if (!slot.Owner || !slot.Owner->Closing || HasChildren(slot.Owner->Handle) ||
                std::ranges::any_of(slot.Owner->Resources, [](const auto& value) { return !value->Destroyed; }))
            {
                continue;
            }
            slot.Owner.reset();
            if (slot.Generation == std::numeric_limits<std::uint32_t>::max())
            {
                slot.Exhausted = true;
            }
            else
            {
                ++slot.Generation;
            }
            changed = true;
        }
        return changed;
    }
    bool FinalizeModules()
    {
        if (Declarations->Registering)
        {
            return false;
        }
        struct Mutation
        {
            bool* Flag;
            explicit Mutation(bool& flag) noexcept
                : Flag(&flag)
            {
                *Flag = true;
            }
            ~Mutation() { *Flag = false; }
            Mutation(const Mutation&) = delete;
            Mutation& operator=(const Mutation&) = delete;
            Mutation(Mutation&&) = delete;
            Mutation& operator=(Mutation&&) = delete;
        };
        const Mutation mutation(Declarations->Registering);
        const auto order = ModuleOrder(false);
        if (!order)
        {
            return false;
        }
        bool changed = false;
        for (auto id = order->rbegin(); id != order->rend(); ++id)
        {
            auto* module = Find(*id);
            if (module->Record->State != StaticModuleState::kCleanupPending || InUse(*id))
            {
                continue;
            }
            const bool alive = std::ranges::any_of(
                Slots,
                [&](const auto& slot)
                {
                    return slot.Owner &&
                           std::ranges::any_of(slot.Owner->Resources, [&](const auto& resource)
                                               { return resource->Owner == *id && !resource->Destroyed; });
                });
            if (alive)
            {
                continue;
            }
            std::erase_if(Declarations->Recipes,
                          [&](const auto& recipe) { return recipe->HostDomain == Domain && recipe->Owner == *id; });
            std::erase_if(Declarations->Published, [&](const auto& declaration)
                          { return declaration.HostDomain == Domain && declaration.Owner == *id; });
            module->ServiceDependencies.clear();
            module->Record->State = StaticModuleState::kUnloaded;
            changed = true;
        }
        return changed;
    }
    void Cleanup(bool finalizeModules)
    {
        std::vector<std::uint64_t> polled;
        bool progress = true;
        while (progress)
        {
            progress = RetireScopes();
            struct Entry
            {
                Scope* Owner;
                Resource* Value;
            };
            std::vector<Entry> resources;
            for (auto const& slot : Slots)
            {
                if (slot.Owner)
                {
                    for (auto const& resource : slot.Owner->Resources)
                    {
                        if (resource->Closing && !resource->Destroyed)
                        {
                            resources.push_back({.Owner = slot.Owner.get(), .Value = resource.get()});
                        }
                    }
                }
            }
            std::ranges::sort(resources, [](const auto& first, const auto& second)
                              { return first.Value->Serial > second.Value->Serial; });
            for (const auto& entry : resources)
            {
                auto& resource = *entry.Value;
                if ((entry.Owner->Closing && HasChildren(entry.Owner->Handle)) ||
                    HasDependent(resource, entry.Owner->Handle))
                {
                    continue;
                }
                if (resource.StartAttempted && !resource.StopRequested)
                {
                    resource.StopRequested = true;
                    resource.Object->RequestStop();
                }
                if (!resource.StartAttempted)
                {
                    resource.Stopped = true;
                }
                if (!resource.Stopped && std::ranges::find(polled, resource.Serial) == polled.end())
                {
                    polled.push_back(resource.Serial);
                    const auto stopped = resource.Object->PollStopped();
                    if (stopped)
                    {
                        resource.Stopped = true;
                        resource.Error.reset();
                    }
                    else
                    {
                        resource.Error = stopped.error();
                    }
                }
                if (!resource.Stopped)
                {
                    continue;
                }
                if (resource.Control->Gate.Count() != 0)
                {
                    resource.Error = MakeModuleError(ModuleError::kBusy);
                    continue;
                }
                resource.Control->Object = nullptr;
                resource.Object.reset();
                resource.Dependencies.clear();
                resource.Destroyed = true;
                resource.Error.reset();
                progress = true;
            }
            if (finalizeModules)
            {
                progress |= FinalizeModules();
            }
            progress |= RetireScopes();
        }
        // Destroyed nodes no longer have inbound graph references: a Provider is
        // destroyed only after all live dependants released their edges.
        for (auto const& slot : Slots)
        {
            if (slot.Owner)
            {
                std::erase_if(slot.Owner->Resources, [](const auto& resource) { return resource->Destroyed; });
            }
        }
    }
    pg::Error PendingError(ModuleId module = ModuleId(), ScopeHandle scope = ScopeHandle())
    {
        for (auto const& slot : Slots)
        {
            if (!slot.Owner || (scope.IsValid() && !Descendant(*slot.Owner, scope)))
            {
                continue;
            }
            for (const auto& resource : slot.Owner->Resources)
            {
                if (resource->Destroyed || !resource->Closing || (module.IsValid() && resource->Owner != module))
                {
                    continue;
                }
                if (resource->Error && *resource->Error != MakeModuleError(ModuleError::kBusy))
                {
                    return *resource->Error;
                }
            }
        }
        return MakeModuleError(ModuleError::kBusy);
    }
    [[nodiscard]] bool OwnsLiveResources(ModuleId id) const
    {
        return std::ranges::any_of(
            Slots,
            [&](const auto& slot)
            {
                return slot.Owner && std::ranges::any_of(slot.Owner->Resources, [&](const auto& resource)
                                                         { return resource->Owner == id && !resource->Destroyed; });
            });
    }
    [[nodiscard]] std::optional<pg::Error> OriginalFailure() const
    {
        for (const auto& module : Modules)
        {
            if (module.Original && module.Record->State == StaticModuleState::kCleanupPending)
            {
                return module.Original;
            }
        }
        for (const auto& slot : Slots)
        {
            if (slot.Owner && slot.Owner->Closing && slot.Owner->Original)
            {
                return slot.Owner->Original;
            }
        }
        return std::nullopt;
    }
    void Shutdown()
    {
        if (!ShuttingDown)
        {
            ShutdownOriginal = OriginalFailure();
        }
        ShuttingDown = true;
        for (auto const& slot : Slots)
        {
            if (slot.Owner)
            {
                slot.Owner->Closing = true;
                slot.Owner->Gate->Close();
                for (auto const& resource : slot.Owner->Resources)
                {
                    if (!resource->Destroyed)
                    {
                        Close(*resource);
                    }
                }
            }
        }
        for (auto& module : Modules)
        {
            if (module.Record->State != StaticModuleState::kUnloaded)
            {
                CloseModule(module);
            }
        }
    }
    [[nodiscard]] bool Finished() const
    {
        return std::ranges::none_of(Slots, [](const auto& slot) { return slot.Owner != nullptr; }) &&
               std::ranges::all_of(Modules, [](const auto& module)
                                   { return module.Record->State == StaticModuleState::kUnloaded; });
    }
};

ScopeRuntime::ScopeRuntime(std::uint64_t domain, RegistrationScope& declarations, std::span<StaticModuleRecord> modules)
    : Data(std::make_unique<State>(domain, declarations, modules))
{
}
ScopeRuntime::~ScopeRuntime()
{
    if (Data->Operating)
    {
        std::terminate();
    }
    Data->Operating = true;
    Data->Shutdown();
    Data->Cleanup(true);
    if (!Data->Finished())
    {
        std::terminate();
    }
}
bool ScopeRuntime::TryBeginOperation() noexcept
{
    if (Data->Operating)
    {
        return false;
    }
    Data->Operating = true;
    return true;
}
void ScopeRuntime::EndOperation() noexcept { Data->Operating = false; }
bool ScopeRuntime::IsShuttingDown() const noexcept { return Data->ShuttingDown; }
void ScopeRuntime::RegistrationFailed(ModuleId id, pg::Error error) noexcept
{
    if (auto* module = Data->Find(id))
    {
        module->Original = error;
        Data->CloseModule(*module);
    }
}
ScopeHandle ScopeRuntime::HostScope() const noexcept
{
    return Data->Slots.front().Owner ? Data->Slots.front().Owner->Handle : ScopeHandle{};
}

pg::Result<ScopeHandle> ScopeRuntime::CreateScope(Kind kind, ScopeHandle parent, ModuleOperationReport& report)
{
    State::Operation const operation(*Data);
    if (!operation.Accepted)
    {
        return std::unexpected(Fail(report, ModuleError::kBusy).error());
    }
    if (Data->ShuttingDown)
    {
        return std::unexpected(Fail(report, ModuleError::kInvalidState).error());
    }
    auto const* ancestor = Data->Find(parent);
    if (ancestor == nullptr)
    {
        return std::unexpected(Fail(report, ModuleError::kScopeNotFound, ModuleId(), parent).error());
    }
    if ((kind != Kind::kContext && kind != Kind::kWorld) || (kind == Kind::kContext && ancestor->Type != Kind::kHost) ||
        (kind == Kind::kWorld && ancestor->Type != Kind::kContext))
    {
        return std::unexpected(Fail(report, ModuleError::kInvalidScope, ModuleId(), parent).error());
    }
    if (ancestor->Closing || !ancestor->Ready)
    {
        return std::unexpected(Fail(report, ModuleError::kNotReady, ModuleId(), parent).error());
    }
    const auto order = Data->ModuleOrder(false);
    if (!order)
    {
        Snapshot(report, ModuleOperationProgress::kUnchanged, ModuleId(), ScopeHandle(), order.error());
        return std::unexpected(order.error());
    }
    auto const slot =
        std::ranges::find_if(Data->Slots, [](const auto& value) { return !value.Owner && !value.Exhausted; });
    std::size_t index = static_cast<std::size_t>(slot - Data->Slots.begin());
    if (slot == Data->Slots.end())
    {
        if (Data->Slots.size() == kMaxScopes)
        {
            return std::unexpected(Fail(report, ModuleError::kLimitExceeded).error());
        }
        Data->Slots.emplace_back();
        index = Data->Slots.size() - 1;
    }
    auto& target = Data->Slots.at(index);
    target.Owner = std::make_unique<State::Scope>();
    target.Owner->Type = kind;
    target.Owner->Parent = parent;
    target.Owner->Handle = {
        .Domain = Data->Domain,
        .Slot = static_cast<std::uint32_t>(index + 1),
        .Generation = target.Generation,
    };
    const auto handle = target.Owner->Handle;
    auto* scope = target.Owner.get();
    for (const auto id : *order)
    {
        if (Data->Find(id)->Record->State != StaticModuleState::kActive)
        {
            continue;
        }
        if (const auto built = Data->Build(id, *scope); !built)
        {
            const auto error = built.error();
            const auto declaration = Data->BuildDeclaration;
            scope->Original = error;
            Data->CloseScope(*scope);
            Data->Cleanup(false);
            const bool pending = Data->Find(handle) != nullptr;
            const std::optional<pg::Error> cleanup =
                pending ? std::optional(Data->PendingError(ModuleId(), handle)) : std::nullopt;
            Snapshot(report, pending ? ModuleOperationProgress::kCleanupPending : ModuleOperationProgress::kRolledBack,
                     id, handle, cleanup, error, declaration);
            return std::unexpected(error);
        }
    }
    for (auto const& resource : scope->Resources)
    {
        resource->Control->Gate.Open();
    }
    scope->Ready = true;
    scope->Delivered = true;
    scope->Gate->Open();
    Snapshot(report, ModuleOperationProgress::kScopeReady, ModuleId(), handle);
    return handle;
}

pg::Status ScopeRuntime::Activate(ModuleId id, ModuleOperationReport& report)
{
    State::Operation const operation(*Data);
    if (!operation.Accepted)
    {
        return Fail(report, ModuleError::kBusy, id);
    }
    auto* module = Data->Find(id);
    if (module == nullptr)
    {
        return Fail(report, ModuleError::kNotFound, id);
    }
    if (Data->ShuttingDown || module->Record->State != StaticModuleState::kRegistered)
    {
        return Fail(report, ModuleError::kInvalidState, id);
    }
    const auto order = Data->ModuleOrder(true, id);
    if (!order)
    {
        Snapshot(report, ModuleOperationProgress::kUnchanged, id, ScopeHandle(), order.error());
        return std::unexpected(order.error());
    }
    auto dependencies = Data->ResolveDependencies(*module);
    if (!dependencies)
    {
        Snapshot(report, ModuleOperationProgress::kUnchanged, id, ScopeHandle(), dependencies.error());
        return std::unexpected(dependencies.error());
    }
    for (const auto kind : {Kind::kHost, Kind::kContext, Kind::kWorld})
    {
        const auto recipes = Data->RecipeOrder(id, kind);
        if (!recipes)
        {
            Snapshot(report, ModuleOperationProgress::kUnchanged, id, ScopeHandle(), recipes.error());
            return std::unexpected(recipes.error());
        }
    }
    module->ServiceDependencies = std::move(*dependencies);
    std::vector<State::Scope*> scopes;
    for (auto const& slot : Data->Slots)
    {
        if (slot.Owner && slot.Owner->Ready && !slot.Owner->Closing)
        {
            scopes.push_back(slot.Owner.get());
        }
    }
    std::ranges::stable_sort(scopes, [](const auto* first, const auto* second) { return first->Type < second->Type; });
    for (auto* scope : scopes)
    {
        if (const auto built = Data->Build(id, *scope); !built)
        {
            const auto error = built.error();
            const auto handle = scope->Handle;
            const auto declaration = Data->BuildDeclaration;
            module->Original = error;
            Data->CloseModule(*module);
            Data->Cleanup(false);
            const std::optional<pg::Error> cleanup =
                Data->OwnsLiveResources(id) ? std::optional(Data->PendingError(id)) : std::nullopt;
            Snapshot(report, ModuleOperationProgress::kCleanupPending, id, handle, cleanup, error, declaration);
            return std::unexpected(error);
        }
    }
    for (auto const& slot : Data->Slots)
    {
        if (slot.Owner)
        {
            for (auto const& resource : slot.Owner->Resources)
            {
                if (resource->Owner == id && !resource->Destroyed)
                {
                    resource->Control->Gate.Open();
                }
            }
        }
    }
    module->Record->State = StaticModuleState::kActive;
    module->Gate->Open();
    Snapshot(report, ModuleOperationProgress::kActive, id);
    return {};
}
pg::Status ScopeRuntime::Load(ModuleId id, ModuleOperationReport& report)
{
    State::Operation const operation(*Data);
    if (!operation.Accepted)
    {
        return Fail(report, ModuleError::kBusy, id);
    }
    auto* module = Data->Find(id);
    if (module == nullptr)
    {
        return Fail(report, ModuleError::kNotFound, id);
    }
    if (Data->ShuttingDown || module->Record->State != StaticModuleState::kUnloaded)
    {
        return Fail(report, ModuleError::kInvalidState, id);
    }
    for (const auto& dependency : module->Record->Bindings)
    {
        const auto* provider = Data->Find(dependency.Dependency);
        if (dependency.IsBound() &&
            ((provider == nullptr) || provider->Record->State == StaticModuleState::kCleanupPending ||
             provider->Record->State == StaticModuleState::kUnloaded))
        {
            return Fail(report, ModuleError::kDependencyNotRegistered, id, ScopeHandle(), dependency.Dependency);
        }
    }
    module->Gate = std::make_shared<ScopeGate>();
    module->Original.reset();
    module->ServiceDependencies.clear();
    module->Record->State = StaticModuleState::kLoaded;
    Snapshot(report, ModuleOperationProgress::kLoaded, id);
    return {};
}
pg::Status ScopeRuntime::BeginUnload(ModuleId id, ModuleOperationReport& report)
{
    State::Operation const operation(*Data);
    if (!operation.Accepted)
    {
        return Fail(report, ModuleError::kBusy, id);
    }
    auto* module = Data->Find(id);
    if (module == nullptr)
    {
        return Fail(report, ModuleError::kNotFound, id);
    }
    if (module->Record->State == StaticModuleState::kUnloaded)
    {
        return Fail(report, ModuleError::kInvalidState, id);
    }
    if (module->Record->State != StaticModuleState::kCleanupPending && Data->InUse(id))
    {
        return Fail(report, ModuleError::kDependencyInUse, id);
    }
    Data->CloseModule(*module);
    Snapshot(report, ModuleOperationProgress::kCleanupPending, id, ScopeHandle(), std::nullopt, module->Original);
    return {};
}
pg::Status ScopeRuntime::TryFinishUnload(ModuleId id, ModuleOperationReport& report)
{
    State::Operation const operation(*Data);
    if (!operation.Accepted)
    {
        return Fail(report, ModuleError::kBusy, id);
    }
    auto const* module = Data->Find(id);
    if (module == nullptr)
    {
        return Fail(report, ModuleError::kNotFound, id);
    }
    if (module->Record->State != StaticModuleState::kCleanupPending)
    {
        return Fail(report, ModuleError::kInvalidState, id);
    }
    const auto original = module->Original;
    Data->Cleanup(true);
    if (module->Record->State != StaticModuleState::kUnloaded)
    {
        const auto error = Data->PendingError(id);
        Snapshot(report, ModuleOperationProgress::kCleanupPending, id, ScopeHandle(), error, original);
        return std::unexpected(error);
    }
    Snapshot(report, ModuleOperationProgress::kUnloaded, id, ScopeHandle(), std::nullopt, original);
    return {};
}
pg::Status ScopeRuntime::BeginCloseScope(ScopeHandle handle, ModuleOperationReport& report)
{
    State::Operation const operation(*Data);
    if (!operation.Accepted)
    {
        return Fail(report, ModuleError::kBusy, ModuleId(), handle);
    }
    auto* scope = Data->Find(handle);
    if ((scope == nullptr) || !scope->Delivered)
    {
        return Fail(report, ModuleError::kScopeNotFound, ModuleId(), handle);
    }
    if (scope->Type == Kind::kHost)
    {
        return Fail(report, ModuleError::kInvalidScope, ModuleId(), handle);
    }
    Data->CloseScope(*scope);
    Snapshot(report, ModuleOperationProgress::kCleanupPending, ModuleId(), handle, std::nullopt, scope->Original);
    return {};
}
pg::Status ScopeRuntime::TryFinishCloseScope(ScopeHandle handle, ModuleOperationReport& report)
{
    State::Operation const operation(*Data);
    if (!operation.Accepted)
    {
        return Fail(report, ModuleError::kBusy, ModuleId(), handle);
    }
    auto const* scope = Data->Find(handle);
    if ((scope == nullptr) || !scope->Delivered)
    {
        return Fail(report, ModuleError::kScopeNotFound, ModuleId(), handle);
    }
    if (!scope->Closing || scope->Type == Kind::kHost)
    {
        return Fail(report, ModuleError::kInvalidState, ModuleId(), handle);
    }
    const auto original = scope->Original;
    Data->Cleanup(false);
    if (Data->Find(handle) != nullptr)
    {
        const auto error = Data->PendingError(ModuleId(), handle);
        Snapshot(report, ModuleOperationProgress::kCleanupPending, ModuleId(), handle, error, original);
        return std::unexpected(error);
    }
    Snapshot(report, ModuleOperationProgress::kScopeClosed, ModuleId(), handle, std::nullopt, original);
    return {};
}
pg::Status ScopeRuntime::PollCleanup(ModuleOperationReport& report)
{
    State::Operation const operation(*Data);
    if (!operation.Accepted)
    {
        return Fail(report, ModuleError::kBusy);
    }
    const auto original = Data->ShuttingDown ? Data->ShutdownOriginal : Data->OriginalFailure();
    Data->Cleanup(true);
    const bool pending =
        std::ranges::any_of(Data->Modules, [](const auto& module)
                            { return module.Record->State == StaticModuleState::kCleanupPending; }) ||
        std::ranges::any_of(Data->Slots, [](const auto& slot) { return slot.Owner && slot.Owner->Closing; });
    if (pending)
    {
        const auto error = Data->PendingError();
        Snapshot(report, ModuleOperationProgress::kCleanupPending, ModuleId(), ScopeHandle(), error, original);
        return std::unexpected(error);
    }
    Snapshot(report, ModuleOperationProgress::kUnchanged, ModuleId(), ScopeHandle(), std::nullopt, original);
    return {};
}
pg::Status ScopeRuntime::BeginShutdown(ModuleOperationReport& report)
{
    State::Operation const operation(*Data);
    if (!operation.Accepted)
    {
        return Fail(report, ModuleError::kBusy);
    }
    Data->Shutdown();
    Snapshot(report, ModuleOperationProgress::kCleanupPending, ModuleId(), ScopeHandle(), std::nullopt,
             Data->ShutdownOriginal);
    return {};
}
pg::Status ScopeRuntime::TryFinishShutdown(ModuleOperationReport& report)
{
    State::Operation const operation(*Data);
    if (!operation.Accepted)
    {
        return Fail(report, ModuleError::kBusy);
    }
    if (!Data->ShuttingDown)
    {
        return Fail(report, ModuleError::kInvalidState);
    }
    const auto original = Data->ShutdownOriginal;
    Data->Cleanup(true);
    if (!Data->Finished())
    {
        const auto error = Data->PendingError();
        Snapshot(report, ModuleOperationProgress::kCleanupPending, ModuleId(), ScopeHandle(), error, original);
        return std::unexpected(error);
    }
    Snapshot(report, ModuleOperationProgress::kUnloaded, ModuleId(), ScopeHandle(), std::nullopt, original);
    return {};
}
} // namespace pg::module::detail
