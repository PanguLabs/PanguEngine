#include "ScopeRuntime.h"
#include "Error.h"
#include "FailureReport.h"
#include "LogWriter.h"
#include "ModuleDiagnostics.h"
#include "ModuleDiagnosticsInternal.h"
#include <Check.h>
#include <ScopeResourceAccess.h>
#include <array>
#include <ranges>
#include <typeinfo>

#include "ModuleRecord.h"
#include <ModuleDeclaration.h>
#include <ModuleError.h>
#include <ModuleHandle.h>
#include <ModuleIdentity.h>
#include <ModuleManager.h>
#include <ModuleOperation.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <ScopeAccess.h>
#include <ScopeBuildContext.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
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
    FailureContext(report.Failure).SetPrimary(error, "scope operation rejected");
    report.FailureOrigin = ModuleFailureOrigin::kRuntime;
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
        ModuleRecord* Record{};
        std::shared_ptr<ScopeGate> Gate;
        std::vector<ModuleId> ServiceDependencies;
        std::optional<pg::Error> Original;
        bool WaitingDependency{};
    };
    struct Resource
    {
        ModuleId Owner{};
        DeclarationId Id{};
        std::uint64_t Serial{};
        std::unique_ptr<ErasedScopeObject> Object;
        // 仅活资源持有业务 RTTI；残留访问控制块不保存 DLL 元数据。
        const std::type_info* TypeInfo{};
        std::shared_ptr<ScopeObjectControl> Control;
        std::vector<Resource*> Dependencies;
        bool Ready{};
        bool StartAttempted{};
        bool Closing{};
        bool StopRequested{};
        bool Stopped{};
        bool Destroyed{};
        std::optional<pg::Error> Error;
        std::optional<ModuleWaitKind> Waiting;
    };
    struct Scope
    {
        ScopeHandle Handle{};
        Kind Type{};
        ScopeHandle Parent{};
        std::shared_ptr<ScopeGate> Gate = MakeScopeGate();
        std::vector<std::unique_ptr<Resource>> Resources;
        bool Ready{};
        bool Closing{};
        bool Delivered{};
        std::optional<pg::Error> Original;
        ModuleId OriginalModule{};
        DeclarationId OriginalDeclaration{};
        bool FailureHistoryTruncated{};
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
    bool CleanupBlocked{};
    std::optional<pg::Error> ShutdownOriginal;
    using HistoryDiagnostic = ModuleHistoryDiagnostic;
    std::array<HistoryDiagnostic, 8> History;
    bool Remember(ModuleHandle owner, ModuleId module, ScopeHandle scope, DeclarationId declaration,
                  const FailureReport& failure, ModuleFailureOrigin origin, bool scopeOwner = false) noexcept
    {
        auto slot = std::ranges::find_if(
            History, [&](const auto& entry)
            { return entry.Used && (scopeOwner ? entry.ScopeOwner == scope : entry.ModuleOwner == owner); });
        if (slot == History.end())
        {
            slot = std::ranges::find(History, false, &HistoryDiagnostic::Used);
        }
        if (slot == History.end())
        {
            return false;
        }
        *slot = {.ModuleOwner = scopeOwner ? ModuleHandle{} : owner,
                 .ScopeOwner = scopeOwner ? scope : ScopeHandle{},
                 .Module = module,
                 .Scope = scope,
                 .Declaration = declaration,
                 .Failure = failure,
                 .Origin = origin,
                 .Used = true};
        return true;
    }
    [[nodiscard]] const HistoryDiagnostic* ModuleHistory(ModuleId id) const noexcept
    {
        const auto found =
            std::ranges::find_if(History, [&](const auto& entry)
                                 { return entry.Used && entry.ModuleOwner.IsValid() && entry.Module == id; });
        return found == History.end() ? nullptr : &*found;
    }
    [[nodiscard]] const HistoryDiagnostic* ScopeHistory(ScopeHandle scope) const noexcept
    {
        const auto found =
            std::ranges::find_if(History, [&](const auto& entry) { return entry.Used && entry.ScopeOwner == scope; });
        return found == History.end() ? nullptr : &*found;
    }
    void Forget(ModuleHandle module, ScopeHandle scope = {}) noexcept
    {
        for (auto& entry : History)
        {
            if (entry.Used &&
                ((module.IsValid() && entry.ModuleOwner == module) || (scope.IsValid() && entry.ScopeOwner == scope)))
            {
                entry.Used = false;
            }
        }
    }
    using PollDiagnostic = ModulePollDiagnostic;
    std::array<PollDiagnostic, kMaxModuleWaitReasons> PollDiagnostics;
    std::size_t PollDiagnosticCount{};
    bool PollDiagnosticsTruncated{};
    FailureReport PollPrimary;
    ModuleFailureOrigin PollPrimaryOrigin{ModuleFailureOrigin::kUnknown};
    ScopeHandle PollPrimaryScope{};
    std::uint64_t PollPrimarySerial{};
    ModuleId PollPrimaryModule{};
    DeclarationId PollPrimaryDeclaration{};
    DeclarationId BuildDeclaration{};
    log::LogWriter Writer;
    FailureReport ShutdownFailure;
    ModuleFailureOrigin ShutdownOrigin{ModuleFailureOrigin::kUnknown};
    ModuleId ShutdownModule{};
    ScopeHandle ShutdownScope{};
    DeclarationId ShutdownDeclaration{};

    State(std::uint64_t domain, RegistrationScope& declarations, std::span<ModuleRecord* const> records,
          log::LogWriter writer)
        : Domain(domain)
        , Declarations(&declarations)
        , Writer(std::move(writer))
    {
        for (auto* record : records)
        {
            Module module;
            module.Record = record;
            module.Gate = record->Code->Gate;
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
    [[nodiscard]] Recipe* Definition(DeclarationId id) const
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
    bool HasOwnedDescendants(ScopeHandle handle, ModuleId owner)
    {
        return std::ranges::any_of(
            Slots,
            [&](const auto& slot)
            {
                return slot.Owner && slot.Owner->Handle != handle && Descendant(*slot.Owner, handle) &&
                       std::ranges::any_of(slot.Owner->Resources, [&](const auto& resource)
                                           { return resource->Owner == owner && !resource->Destroyed; });
            });
    }
    bool HasChildren(ScopeHandle handle)
    {
        return std::ranges::any_of(Slots, [&](const auto& slot) { return slot.Owner && slot.Owner->Parent == handle; });
    }

    static std::vector<ModuleId> Dependencies(Module& module)
    {
        // 常驻记录的资源与外部代码使用都排空后，才解除其业务 Provider 保护。
        // 可卸载记录仍须保留依赖，直到 Manager 成功完成原生释放并 Detach。
        if (module.Record->Resident && module.Record->ResourcesDrained && !module.Record->Code->HasExternalUsers())
        {
            return {};
        }
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
                if ((owner == nullptr) || owner->Record->State == ModuleState::kUnloaded)
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
                                           module.Record->State == ModuleState::kUnloaded)
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
            if ((provider == nullptr) || provider->Record->State != ModuleState::kActive)
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
                    if ((owner == nullptr) || owner->Record->State != ModuleState::kActive)
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
            resource->TypeInfo = &resource->Object->Type();
            resource->Dependencies = Providers;
            resource->Control = MakeScopeObjectControl(Runtime->Find(Definition->Owner)->Gate, TargetScope->Gate,
                                                       resource->Object->Get());
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

    [[nodiscard]] pg::Result<std::vector<Recipe*>> RecipeOrder(ModuleId owner, Kind kind) const
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

    [[nodiscard]] pg::Status Build(ModuleId owner, Scope& scope, FailureReport& failure, ModuleFailureOrigin& origin)
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
            failure.Clear();
            FailureContext diagnostics(failure);
            origin = ModuleFailureOrigin::kFactory;
            ScopeBuildContext context(frame, diagnostics, TaggedWriter(Writer, owner, scope.Handle, recipe->Id));
            const auto result = recipe->Factory(context);
            if (frame.Failure || !result)
            {
                const auto error = frame.Failure ? *frame.Failure : result.error();
                MatchFailure(failure, error, origin);
                return std::unexpected(error);
            }
            if (frame.Owned == nullptr)
            {
                const auto error = MakeModuleError(ModuleError::kInvalidDeclaration);
                diagnostics.SetPrimary(error, "factory did not own an object");
                origin = ModuleFailureOrigin::kRuntime;
                return std::unexpected(error);
            }
            frame.Owned->StartAttempted = true;
            failure.Clear();
            origin = ModuleFailureOrigin::kStart;
            if (const auto started = frame.Owned->Object->Start(diagnostics); !started)
            {
                MatchFailure(failure, started.error(), origin);
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
        module.Record->State = ModuleState::kCleanupPending;
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
            Forget({}, slot.Owner->Handle);
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
    bool FinalizeModules(ModuleId target)
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
        for (auto id : std::views::reverse(*order))
        {
            auto* module = Find(id);
            if ((target.IsValid() && id != target) || module->Record->ResourcesDrained ||
                module->Record->State != ModuleState::kCleanupPending)
            {
                continue;
            }
            module->WaitingDependency = InUse(id);
            if (module->WaitingDependency)
            {
                continue;
            }
            const bool alive = std::ranges::any_of(
                Slots,
                [&](const auto& slot)
                {
                    return slot.Owner && std::ranges::any_of(slot.Owner->Resources, [&](const auto& resource)
                                                             { return resource->Owner == id && !resource->Destroyed; });
                });
            if (alive)
            {
                continue;
            }
            std::erase_if(Declarations->Recipes,
                          [&](const auto& recipe) { return recipe->HostDomain == Domain && recipe->Owner == id; });
            std::erase_if(Declarations->Published, [&](const auto& declaration)
                          { return declaration.HostDomain == Domain && declaration.Owner == id; });
            // 原生释放完成前保留有效服务依赖，不能提前清理 Provider。
            module->Record->ResourcesDrained = true;
            changed = true;
        }
        return changed;
    }
    void Cleanup(bool finalizeModules, ModuleId target = {}, ScopeHandle diagnosticScope = {})
    {
        // 包括 CreateScope/Activate 的内部失败回滚，以及无资源 Scope 的槽位回收。
        if (CleanupBlocked)
        {
            return;
        }
        PollDiagnosticCount = 0;
        PollDiagnosticsTruncated = false;
        PollPrimary.Clear();
        PollPrimaryOrigin = ModuleFailureOrigin::kUnknown;
        PollPrimaryScope = {};
        PollPrimarySerial = 0;
        PollPrimaryModule = {};
        PollPrimaryDeclaration = {};
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
                        if (resource->Closing && !resource->Destroyed &&
                            (target.IsValid() ? resource->Owner == target
                                              : Find(resource->Owner)->Record->State != ModuleState::kCleanupPending))
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
                resource.Waiting.reset();
                if (target.IsValid() && InUse(resource.Owner))
                {
                    resource.Waiting = ModuleWaitKind::kDependency;
                    continue;
                }
                if (entry.Owner->Closing && (ShuttingDown ? HasOwnedDescendants(entry.Owner->Handle, resource.Owner)
                                                          : HasChildren(entry.Owner->Handle)))
                {
                    resource.Waiting = ModuleWaitKind::kChildScope;
                    continue;
                }
                if (HasDependent(resource, entry.Owner->Handle))
                {
                    resource.Waiting = ModuleWaitKind::kDependency;
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
                    FailureReport failure;
                    FailureContext diagnostics(failure);
                    const auto stopped = resource.Object->PollStopped(diagnostics);
                    if (stopped)
                    {
                        resource.Stopped = true;
                        resource.Error.reset();
                    }
                    else
                    {
                        resource.Error = stopped.error();
                        auto origin = ModuleFailureOrigin::kPollStopped;
                        MatchFailure(failure, stopped.error(), origin);
                        if (stopped.error() != MakeModuleError(ModuleError::kBusy) &&
                            (!diagnosticScope.IsValid() || Descendant(*entry.Owner, diagnosticScope)) &&
                            (!PollPrimary.PrimarySite() || entry.Owner->Handle.Slot < PollPrimaryScope.Slot ||
                             (entry.Owner->Handle == PollPrimaryScope && resource.Serial < PollPrimarySerial)))
                        {
                            PollPrimary = failure;
                            PollPrimaryOrigin = origin;
                            PollPrimaryScope = entry.Owner->Handle;
                            PollPrimarySerial = resource.Serial;
                            PollPrimaryModule = resource.Owner;
                            PollPrimaryDeclaration = resource.Id;
                        }
                        if (PollDiagnosticCount < PollDiagnostics.size())
                        {
                            PollDiagnostics.at(PollDiagnosticCount++) = {.Serial = resource.Serial,
                                                                         .Site = failure.PrimarySite()};
                        }
                        else
                        {
                            PollDiagnosticsTruncated = true;
                        }
                    }
                }
                if (!resource.Stopped)
                {
                    resource.Waiting = ModuleWaitKind::kStop;
                    continue;
                }
                if (resource.Control->Gate.Count() != 0)
                {
                    resource.Error = MakeModuleError(ModuleError::kBusy);
                    resource.Waiting = ModuleWaitKind::kBorrow;
                    continue;
                }
                resource.Control->Object = nullptr;
                resource.Object.reset();
                resource.TypeInfo = nullptr;
                resource.Dependencies.clear();
                resource.Destroyed = true;
                resource.Error.reset();
                progress = true;
            }
            if (finalizeModules)
            {
                progress |= FinalizeModules(target);
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
    [[nodiscard]] bool FindScopeDescendant(ScopeHandle child, ScopeHandle ancestor) const noexcept
    {
        while (child.IsValid() && child.Domain == Domain && child.Slot <= Slots.size())
        {
            if (child == ancestor)
            {
                return true;
            }
            const auto& slot = Slots.at(child.Slot - 1);
            if (!slot.Owner || slot.Owner->Handle != child)
            {
                return false;
            }
            child = slot.Owner->Parent;
        }
        return false;
    }
    void Explain(ModuleOperationReport& report, ModuleId waitModule = {}, ScopeHandle waitScope = {}) const noexcept
    {
        const auto choose = [&](const FailureReport& failure, ModuleFailureOrigin origin)
        {
            const auto site = failure.PrimarySite();
            if (site && report.PrimaryError && site->Cause == *report.PrimaryError && !report.Failure.PrimarySite())
            {
                report.Failure = failure;
                report.FailureOrigin = origin;
            }
        };
        ModuleId originModule = report.Module;
        ScopeHandle originScope = report.Scope;
        DeclarationId originDeclaration = report.Declaration;
        const HistoryDiagnostic* selectedHistory{};
        if (report.OriginalError)
        {
            if (report.Module.IsValid())
            {
                selectedHistory = ModuleHistory(report.Module);
            }
            if (selectedHistory == nullptr && report.Scope.IsValid())
            {
                selectedHistory = ScopeHistory(report.Scope);
            }
            if (!report.Module.IsValid() && !report.Scope.IsValid())
            {
                bool selected{};
                for (const auto& module : Modules)
                {
                    if (module.Original == report.OriginalError && module.Record->State == ModuleState::kCleanupPending)
                    {
                        originModule = module.Record->OriginalModule;
                        originScope = module.Record->OriginalScope;
                        originDeclaration = module.Record->OriginalDeclaration;
                        selectedHistory = ModuleHistory(originModule);
                        selected = true;
                        break;
                    }
                }
                if (!selected)
                {
                    for (const auto& slot : Slots)
                    {
                        if (slot.Owner && slot.Owner->Closing && slot.Owner->Original == report.OriginalError)
                        {
                            originModule = slot.Owner->OriginalModule;
                            originScope = slot.Owner->Handle;
                            originDeclaration = slot.Owner->OriginalDeclaration;
                            selectedHistory = ScopeHistory(originScope);
                            break;
                        }
                    }
                }
            }
            if (selectedHistory != nullptr)
            {
                choose(selectedHistory->Failure, selectedHistory->Origin);
                originModule = selectedHistory->Module;
                originScope = selectedHistory->Scope;
                originDeclaration = selectedHistory->Declaration;
            }
            else if (ShuttingDown && report.OriginalError == ShutdownOriginal)
            {
                choose(ShutdownFailure, ShutdownOrigin);
                originModule = ShutdownModule;
                originScope = ShutdownScope;
                originDeclaration = ShutdownDeclaration;
            }
        }
        const bool pollMatches = (!waitModule.IsValid() || waitModule == PollPrimaryModule) &&
                                 (!waitScope.IsValid() || waitScope == PollPrimaryScope ||
                                  (PollPrimaryScope.IsValid() && FindScopeDescendant(PollPrimaryScope, waitScope)));
        if (pollMatches)
        {
            // 清理现场只能补充原错，不能因 Error 同值而冒充缺失的原始现场。
            if (!report.OriginalError)
            {
                choose(PollPrimary, PollPrimaryOrigin);
            }
            if (report.CleanupError)
            {
                const auto primary = PollPrimary.PrimarySite();
                if (primary && primary->Cause == *report.CleanupError)
                {
                    AppendFailure(report.Failure, PollPrimary);
                }
            }
        }
        for (const auto& module : Modules)
        {
            if (module.WaitingDependency && (!waitModule.IsValid() || waitModule == module.Record->Descriptor.Id))
            {
                ModuleId related;
                std::uint64_t count{};
                for (const auto& consumer : Modules)
                {
                    if (consumer.Record->Descriptor.Id == module.Record->Descriptor.Id ||
                        consumer.Record->State == ModuleState::kUnloaded ||
                        (consumer.Record->Resident && consumer.Record->ResourcesDrained &&
                         !consumer.Record->Code->HasExternalUsers()))
                    {
                        continue;
                    }
                    const bool bound = std::ranges::any_of(
                        consumer.Record->Bindings, [&](const auto& binding)
                        { return binding.IsBound() && binding.Dependency == module.Record->Descriptor.Id; });
                    const bool service =
                        std::ranges::find(consumer.ServiceDependencies, module.Record->Descriptor.Id) !=
                        consumer.ServiceDependencies.end();
                    if (bound || service)
                    {
                        if (count == 0)
                        {
                            related = consumer.Record->Descriptor.Id;
                        }
                        ++count;
                    }
                }
                if (count != 0)
                {
                    AddWait(report, {.Kind = ModuleWaitKind::kDependency,
                                     .Module = module.Record->Descriptor.Id,
                                     .RelatedModule = related,
                                     .Count = count});
                }
            }
        }
        for (const auto& slot : Slots)
        {
            if (!slot.Owner)
            {
                continue;
            }
            for (const auto& resource : slot.Owner->Resources)
            {
                if (!resource->Closing || resource->Destroyed ||
                    (waitModule.IsValid() && waitModule != resource->Owner) ||
                    (waitScope.IsValid() && !FindScopeDescendant(slot.Owner->Handle, waitScope)))
                {
                    continue;
                }
                for (std::size_t index = 0; index < PollDiagnosticCount; ++index)
                {
                    const auto& diagnostic = PollDiagnostics.at(index);
                    if (diagnostic.Serial != resource->Serial)
                    {
                        continue;
                    }
                    if (report.CleanupError && resource->Error == report.CleanupError && diagnostic.Site)
                    {
                        FailureContext(report.Failure).AddSecondary(*diagnostic.Site);
                    }
                }
                const auto count = resource->Control->Gate.Count();
                if (count != 0)
                {
                    AddWait(report, {.Kind = ModuleWaitKind::kBorrow,
                                     .Module = resource->Owner,
                                     .Scope = slot.Owner->Handle,
                                     .Declaration = resource->Id,
                                     .Count = count,
                                     .Error = resource->Error});
                }
                if (const auto waiting = resource->Waiting; waiting && *waiting != ModuleWaitKind::kBorrow)
                {
                    AddWait(report, {.Kind = *waiting,
                                     .Module = resource->Owner,
                                     .Scope = slot.Owner->Handle,
                                     .Declaration = resource->Id,
                                     .Count = 1,
                                     .Error = resource->Error});
                }
            }
            if (!waitModule.IsValid() && slot.Owner->Closing &&
                (!waitScope.IsValid() || FindScopeDescendant(slot.Owner->Handle, waitScope)))
            {
                const auto count = std::ranges::count_if(
                    Slots, [&](const auto& child) { return child.Owner && child.Owner->Parent == slot.Owner->Handle; });
                if (count != 0)
                {
                    AddWait(report, {.Kind = ModuleWaitKind::kChildScope,
                                     .Scope = slot.Owner->Handle,
                                     .Count = static_cast<std::uint64_t>(count)});
                }
            }
        }
        if (PollDiagnosticsTruncated)
        {
            FailureContext(report.Failure).AddContext("poll.diagnostics.truncated", "true");
        }
        if (report.OriginalError && !report.Failure.PrimarySite())
        {
            report.FailureHistoryTruncated = true;
            FailureContext(report.Failure).AddContext("history.truncated", "true");
        }
        if (report.OriginalError)
        {
            report.Module = originModule;
            report.Scope = originScope;
            report.Declaration = originDeclaration;
        }
        if (!report.OriginalError && report.PrimaryError)
        {
            const auto primary = PollPrimary.PrimarySite();
            if (pollMatches && primary && primary->Cause == *report.PrimaryError)
            {
                report.Module = PollPrimaryModule;
                report.Scope = PollPrimaryScope;
                report.Declaration = PollPrimaryDeclaration;
            }
        }
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
            if (module.Original && module.Record->State == ModuleState::kCleanupPending)
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
            ModuleOperationReport history{.PrimaryError = ShutdownOriginal, .OriginalError = ShutdownOriginal};
            Explain(history);
            ShutdownFailure = history.Failure;
            ShutdownOrigin = history.FailureOrigin;
            ShutdownModule = history.Module;
            ShutdownScope = history.Scope;
            ShutdownDeclaration = history.Declaration;
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
            if (module.Record->State != ModuleState::kUnloaded)
            {
                CloseModule(module);
            }
        }
    }
    [[nodiscard]] bool Finished() const
    {
        return std::ranges::none_of(Slots, [](const auto& slot) { return slot.Owner != nullptr; }) &&
               std::ranges::all_of(Modules, [](const auto& module) { return module.Record->ResourcesDrained; });
    }
};

ScopeRuntime::ScopeRuntime(std::uint64_t domain, RegistrationScope& declarations,
                           std::span<ModuleRecord* const> modules, log::LogWriter writer)
    : Data(std::make_unique<State>(domain, declarations, modules, std::move(writer)))
{
}
ScopeRuntime::~ScopeRuntime()
{
    if (Data->Operating)
    {
        pg::Fatal("scope lifetime invariant violated");
    }
    if (!Data->Finished())
    {
        pg::Fatal("scope lifetime invariant violated");
    }
}

void ScopeRuntime::AttachModules(std::span<ModuleRecord* const> records) noexcept
{
    for (auto* record : records)
    {
        State::Module module;
        module.Record = record;
        module.Gate = record->Code->Gate;
        Data->Modules.push_back(std::move(module));
    }
}
void ScopeRuntime::DetachModule(ModuleHandle handle) noexcept
{
    Data->Forget(handle);
    std::erase_if(Data->Modules, [&](const auto& module) { return module.Record->Handle == handle; });
}
void ScopeRuntime::RequestShutdown() noexcept { Data->Shutdown(); }
void ScopeRuntime::SetCleanupBlocked(bool blocked) noexcept { Data->CleanupBlocked = blocked; }
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
void ScopeRuntime::Explain(ModuleOperationReport& report, ModuleId waitModule, ScopeHandle waitScope) const noexcept
{
    Data->Explain(report, waitModule, waitScope);
}
bool ScopeRuntime::RememberFailure(ModuleHandle owner, ModuleId module, ScopeHandle scope, DeclarationId declaration,
                                   const FailureReport& failure, ModuleFailureOrigin origin) noexcept
{
    return Data->Remember(owner, module, scope, declaration, failure, origin);
}
bool ScopeRuntime::IsShuttingDown() const noexcept { return Data->ShuttingDown; }
std::optional<pg::Error> ScopeRuntime::PendingFailure() const noexcept
{
    return Data->ShuttingDown ? Data->ShutdownOriginal : Data->OriginalFailure();
}
bool ScopeRuntime::HasBusinessDependency(ModuleHandle handle) const noexcept
{
    const auto found =
        std::ranges::find_if(Data->Modules, [&](const auto& module) { return module.Record->Handle == handle; });
    if (found == Data->Modules.end())
    {
        return false;
    }
    return std::ranges::any_of(Data->Modules,
                               [&](const auto& module)
                               {
                                   return !module.Record->Resident &&
                                          Data->ModuleDependsOn(found->Record->Descriptor.Id,
                                                                module.Record->Descriptor.Id);
                               });
}
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

Result<std::shared_ptr<ScopeObjectControl>>
ScopeRuntime::AcquireScopeResource(const ScopeResourceQuery& query, const std::type_info& type) const noexcept
{
    if (query.Kind != Kind::kHost && query.Kind != Kind::kContext && query.Kind != Kind::kWorld)
    {
        return std::unexpected(MakeModuleError(ModuleError::kInvalidScope));
    }
    auto* scope = Data->Find(query.Scope);
    if (scope == nullptr)
    {
        return std::unexpected(MakeModuleError(ModuleError::kScopeNotFound));
    }
    if (scope->Type != query.Kind)
    {
        return std::unexpected(MakeModuleError(ModuleError::kInvalidScope));
    }
    if (!scope->Delivered || !scope->Ready || scope->Closing)
    {
        return std::unexpected(MakeModuleError(ModuleError::kNotReady));
    }
    const auto* resource = State::FindResource(*scope, query.Declaration);
    if (resource == nullptr || resource->Owner != query.Owner)
    {
        return std::unexpected(MakeModuleError(ModuleError::kNotFound));
    }
    const auto* module = Data->Find(resource->Owner);
    if (module == nullptr || module->Record->State != ModuleState::kActive || !resource->Ready || resource->Closing)
    {
        return std::unexpected(MakeModuleError(ModuleError::kNotReady));
    }
    if (resource->TypeInfo == nullptr || *resource->TypeInfo != type)
    {
        return std::unexpected(MakeModuleError(ModuleError::kTypeMismatch));
    }
    return resource->Control;
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
    FailureReport buildFailure;
    auto buildOrigin = ModuleFailureOrigin::kUnknown;
    for (const auto id : *order)
    {
        if (Data->Find(id)->Record->State != ModuleState::kActive)
        {
            continue;
        }
        if (const auto built = Data->Build(id, *scope, buildFailure, buildOrigin); !built)
        {
            const auto error = built.error();
            const auto declaration = Data->BuildDeclaration;
            MatchFailure(buildFailure, error, buildOrigin);
            scope->Original = error;
            scope->OriginalModule = id;
            scope->OriginalDeclaration = declaration;
            const bool historyTruncated = !Data->Remember({}, id, handle, declaration, buildFailure, buildOrigin, true);
            scope->FailureHistoryTruncated = historyTruncated;
            Data->CloseScope(*scope);
            Data->Cleanup(false, {}, handle);
            const bool pending = Data->Find(handle) != nullptr;
            const std::optional<pg::Error> cleanup =
                pending ? std::optional(Data->PendingError(ModuleId(), handle)) : std::nullopt;
            Snapshot(report, pending ? ModuleOperationProgress::kCleanupPending : ModuleOperationProgress::kRolledBack,
                     id, handle, cleanup, error, declaration);
            report.Failure = buildFailure;
            report.FailureOrigin = buildOrigin;
            report.FailureHistoryTruncated = historyTruncated;
            Data->Explain(report, {}, handle);
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
    if (Data->ShuttingDown || module->Record->State != ModuleState::kRegistered)
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
    FailureReport buildFailure;
    auto buildOrigin = ModuleFailureOrigin::kUnknown;
    for (auto* scope : scopes)
    {
        if (const auto built = Data->Build(id, *scope, buildFailure, buildOrigin); !built)
        {
            const auto error = built.error();
            const auto handle = scope->Handle;
            const auto declaration = Data->BuildDeclaration;
            MatchFailure(buildFailure, error, buildOrigin);
            module->Original = error;
            module->Record->OriginalModule = id;
            module->Record->OriginalScope = handle;
            module->Record->OriginalDeclaration = declaration;
            module->Record->FailureHistoryTruncated =
                !Data->Remember(module->Record->Handle, id, handle, declaration, buildFailure, buildOrigin);
            Data->CloseModule(*module);
            Data->Cleanup(false, id);
            const std::optional<pg::Error> cleanup =
                Data->OwnsLiveResources(id) ? std::optional(Data->PendingError(id)) : std::nullopt;
            Snapshot(report, ModuleOperationProgress::kCleanupPending, id, handle, cleanup, error, declaration);
            report.Failure = buildFailure;
            report.FailureOrigin = buildOrigin;
            report.FailureHistoryTruncated = module->Record->FailureHistoryTruncated;
            Data->Explain(report, id);
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
    module->Record->State = ModuleState::kActive;
    module->Gate->Open();
    Snapshot(report, ModuleOperationProgress::kActive, id);
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
    if (module->Record->State == ModuleState::kUnloaded)
    {
        return Fail(report, ModuleError::kInvalidState, id);
    }
    if (module->Record->State != ModuleState::kCleanupPending && Data->InUse(id))
    {
        module->WaitingDependency = true;
        const auto result = Fail(report, ModuleError::kDependencyInUse, id);
        Data->Explain(report, id);
        return result;
    }
    Data->CloseModule(*module);
    Snapshot(report, ModuleOperationProgress::kCleanupPending, id, ScopeHandle(), std::nullopt, module->Original);
    Data->Explain(report, id);
    return {};
}
pg::Status ScopeRuntime::TryFinishResources(ModuleHandle handle, ModuleOperationReport& report) noexcept
{
    const auto found =
        std::ranges::find_if(Data->Modules, [&](const auto& module) { return module.Record->Handle == handle; });
    if (found == Data->Modules.end())
    {
        return Fail(report, ModuleError::kInvalidHandle);
    }
    const auto id = found->Record->Descriptor.Id;
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
    if (module->Record->State != ModuleState::kCleanupPending)
    {
        return Fail(report, ModuleError::kInvalidState, id);
    }
    const auto original = module->Original;
    Data->Cleanup(true, id);
    if (!module->Record->ResourcesDrained)
    {
        const auto error = Data->PendingError(id);
        Snapshot(report, ModuleOperationProgress::kCleanupPending, id, ScopeHandle(), error, original);
        Data->Explain(report, id);
        return std::unexpected(error);
    }
    Snapshot(report, ModuleOperationProgress::kUnloaded, id, ScopeHandle(), std::nullopt, original);
    Data->Explain(report, id);
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
    Data->Explain(report, {}, handle);
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
    const auto* history = Data->ScopeHistory(handle);
    const auto originalFailure = history != nullptr ? history->Failure : FailureReport{};
    const auto originalOrigin = history != nullptr ? history->Origin : ModuleFailureOrigin::kUnknown;
    const auto originalModule = scope->OriginalModule;
    const auto originalDeclaration = scope->OriginalDeclaration;
    const auto historyTruncated = scope->FailureHistoryTruncated;
    Data->Cleanup(false, {}, handle);
    if (Data->Find(handle) != nullptr)
    {
        const auto error = Data->PendingError(ModuleId(), handle);
        Snapshot(report, ModuleOperationProgress::kCleanupPending, ModuleId(), handle, error, original);
        Data->Explain(report, {}, handle);
        return std::unexpected(error);
    }
    Snapshot(report, ModuleOperationProgress::kScopeClosed, ModuleId(), handle, std::nullopt, original);
    Data->Explain(report, {}, handle);
    report.Failure = originalFailure;
    report.FailureOrigin = originalOrigin;
    report.Module = originalModule;
    report.Declaration = originalDeclaration;
    report.FailureHistoryTruncated = historyTruncated;
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
    Data->Cleanup(false);
    const bool pending =
        std::ranges::any_of(Data->Modules,
                            [](const auto& module) { return module.Record->State == ModuleState::kCleanupPending; }) ||
        std::ranges::any_of(Data->Slots, [](const auto& slot) { return slot.Owner && slot.Owner->Closing; });
    if (pending)
    {
        const auto error = Data->PendingError();
        Snapshot(report, ModuleOperationProgress::kCleanupPending, ModuleId(), ScopeHandle(), error, original);
        Data->Explain(report);
        return std::unexpected(error);
    }
    Snapshot(report, ModuleOperationProgress::kUnchanged, ModuleId(), ScopeHandle(), std::nullopt, original);
    Data->Explain(report);
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
    Data->Explain(report);
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
    Data->Cleanup(false);
    if (!Data->Finished())
    {
        const auto error = Data->PendingError();
        Snapshot(report, ModuleOperationProgress::kCleanupPending, ModuleId(), ScopeHandle(), error, original);
        Data->Explain(report);
        return std::unexpected(error);
    }
    Snapshot(report, ModuleOperationProgress::kUnloaded, ModuleId(), ScopeHandle(), std::nullopt, original);
    Data->Explain(report);
    return {};
}
} // namespace pg::module::detail
