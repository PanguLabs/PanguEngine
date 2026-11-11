#include "Error.h"
#include "ModuleCatalog.h"
#include "ModuleCodeControl.h"
#include "ModuleCodePin.h"
#include "ModuleDescriptor.h"
#include "ModuleHandle.h"
#include "ModuleIdentity.h"
#include "ModuleOperation.h"
#include "ModuleRecord.h"
#include "ModuleTestAccess.h"
#include "Result.h"
#include "ScopeIdentity.h"
#include "ScopeRuntime.h"
#include <FoundationModule.h>
#include <ModuleBuiltin.h>
#include <ModuleError.h>
#include <ModuleManager.h>
#include <ModuleMetaBorrow.h>
#include <ModuleMetaProvider.h>
#include <RegistrationContext.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <typeinfo>
#include <utility>
#include <variant>
#include <vector>

namespace pg::module
{
namespace
{
std::unexpected<Error> Reject(ModuleOperationReport& report, ModuleError code, ModuleHandle handle = ModuleHandle(),
                              ModuleId id = ModuleId())
{
    const auto error = MakeModuleError(code);
    report = {.PrimaryError = error, .Module = id, .Handle = handle};
    return std::unexpected(error);
}
void CompleteScopeReport(ModuleOperationReport& report, ModuleCleanupOwner pendingOwner, bool shuttingDown)
{
    report.CleanupOwner = ModuleCleanupOwner::kNone;
    if (report.Progress == ModuleOperationProgress::kCleanupPending)
    {
        report.CleanupOwner = shuttingDown ? ModuleCleanupOwner::kManagerShutdown : pendingOwner;
    }
}
} // namespace
struct ModuleManager::State
{
    struct Slot
    {
        std::uint64_t Generation{1};
        std::unique_ptr<detail::ModuleRecord> Record;
        std::shared_ptr<detail::ModuleCodeControl> PublishedCode;
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
    RegistrationScope* Declarations;
    std::uint64_t Domain;
    std::array<Slot, kMaxCatalogModules> Slots;
    std::mutex AdmissionMutex;
    std::unique_ptr<detail::ScopeRuntime> Runtime;
    bool Operating{};
    bool Closed{};
    std::optional<Error> ShutdownError;
    bool FailRollbackClose{};
    struct FailedBatch
    {
        std::vector<ModuleHandle> ReleaseOrder;
    };
    std::vector<FailedBatch> FailedBatches;

    State(RegistrationScope& declarations, std::uint64_t domain)
        : Declarations(&declarations)
        , Domain(domain)
        , Runtime(
              std::make_unique<detail::ScopeRuntime>(domain, declarations, std::span<detail::ModuleRecord* const>{}))
    {
    }
    [[nodiscard]] detail::ModuleRecord* Resolve(ModuleHandle handle) const noexcept
    {
        if (handle.Domain != Domain || handle.Slot == 0 || handle.Slot > Slots.size())
        {
            return nullptr;
        }
        const auto& slot = Slots.at(handle.Slot - 1);
        return slot.Record && slot.Generation == handle.Generation ? slot.Record.get() : nullptr;
    }
    [[nodiscard]] std::vector<detail::ModuleRecord*> Records() const
    {
        std::vector<detail::ModuleRecord*> records;
        for (const auto& slot : Slots)
        {
            if (slot.Record)
            {
                records.push_back(slot.Record.get());
            }
        }
        return records;
    }
    void Retire(ModuleHandle handle)
    {
        Runtime->DetachModule(handle);
        std::unique_ptr<detail::ModuleRecord> retired;
        const std::scoped_lock lock(AdmissionMutex);
        auto& slot = Slots.at(handle.Slot - 1);
        slot.Record->Code->Gate->Close();
        slot.PublishedCode.reset();
        retired = std::move(slot.Record);
        if (slot.Generation == std::numeric_limits<std::uint64_t>::max())
        {
            slot.Exhausted = true;
        }
        else
        {
            ++slot.Generation;
        }
    }
    // 拒绝批次从未执行注册；未知依赖项先退出，已知项沿依赖逆序。
    // 无效元数据环只释放自身 OS 持有；原生加载器继续维护真实导入引用。
    static FailedBatch RollbackOrder(std::span<detail::ModuleRecord* const> records)
    {
        FailedBatch batch;
        std::vector<unsigned char> visited(records.size());
        for (std::size_t index = 0; index < records.size(); ++index)
        {
            if (records.subspan(index).front()->RegisterModule == nullptr)
            {
                visited.at(index) = 2;
                batch.ReleaseOrder.push_back(records.subspan(index).front()->Handle);
            }
        }
        // 接纳预算将递归深度限制为最多 256；仅用于冷路径拒绝批次的依赖遍历。
        // NOLINTNEXTLINE(misc-no-recursion)
        const auto visit = [&](auto&& self, std::size_t index) -> void
        {
            if (visited.at(index) != 0)
            {
                return;
            }
            visited.at(index) = 1;
            for (std::size_t dependent = 0; dependent < records.size(); ++dependent)
            {
                if (std::ranges::any_of(records.subspan(dependent).front()->Descriptor.Dependencies,
                                        [&](const auto& dependency)
                                        { return dependency.Id == records.subspan(index).front()->Descriptor.Id; }))
                {
                    self(self, dependent);
                }
            }
            visited.at(index) = 2;
            batch.ReleaseOrder.push_back(records.subspan(index).front()->Handle);
        };
        for (std::size_t index = 0; index < records.size(); ++index)
        {
            visit(visit, index);
        }
        return batch;
    }
    [[nodiscard]] Status ReleaseBatch(const FailedBatch& batch)
    {
        for (const auto handle : batch.ReleaseOrder)
        {
            auto* record = Resolve(handle);
            record->Metadata = nullptr;
            if (record->Library)
            {
                const auto released = record->Library->Release();
                if (!released)
                {
                    return released;
                }
                record->Library.reset();
                // 整组占位可能继续保留，但已释放库的入口不再是有效借用。
                record->RegisterModule = nullptr;
            }
        }
        for (const auto handle : batch.ReleaseOrder)
        {
            Retire(handle);
        }
        return {};
    }
    Result<ModuleLoadResult> Accept(std::span<const ModuleLoadCandidate> candidates,
                                    std::span<const OptionalDependencyBinding> selections,
                                    ModuleOperationReport& report, bool resident)
    {
        report = {};
        if (candidates.empty())
        {
            return Reject(report, ModuleError::kInvalidDescriptor);
        }
        const auto existing = Records();
        const auto capacity =
            std::ranges::count_if(Slots, [](const auto& slot) { return !slot.Record && !slot.Exhausted; });
        if (candidates.size() > static_cast<std::size_t>(capacity) ||
            selections.size() > kMaxCatalogModules * kMaxModuleDependencies)
        {
            return Reject(report, ModuleError::kLimitExceeded);
        }
        std::vector<ModuleId> known;
        for (const auto& candidate : candidates)
        {
            const auto* shared = std::get_if<SharedModuleCandidate>(&candidate);
#ifdef PG_MODULE_MODULAR
            if (!resident && shared == nullptr)
            {
                return Reject(report, ModuleError::kWrongLoadSource);
            }
#else
            if (shared != nullptr)
            {
                return Reject(report, ModuleError::kWrongLoadSource);
            }
#endif
            if (shared != nullptr)
            {
                if (!shared->ExpectedId.IsValid())
                {
                    return Reject(report, ModuleError::kInvalidDescriptor);
                }
                if (std::ranges::find(known, shared->ExpectedId) != known.end() ||
                    std::ranges::any_of(existing, [&](const auto* record)
                                        { return record->Descriptor.Id == shared->ExpectedId; }))
                {
                    return Reject(report, ModuleError::kDuplicateModule, ModuleHandle(), shared->ExpectedId);
                }
                known.push_back(shared->ExpectedId);
            }
        }
        for (const auto& candidate : candidates)
        {
            if (const auto* shared = std::get_if<SharedModuleCandidate>(&candidate))
            {
                if (shared->Path.empty() || !shared->Path.is_absolute() ||
                    shared->Path.native().contains(std::filesystem::path::value_type{}))
                {
                    return Reject(report, ModuleError::kInvalidPath);
                }
            }
            else if (std::get<StaticModuleCandidate>(candidate).Query == nullptr)
            {
                return Reject(report, ModuleError::kInvalidDefinition);
            }
        }
        std::vector<detail::ModuleRecord*> pending;
        for (std::size_t index = 0; index < Slots.size() && pending.size() < candidates.size(); ++index)
        {
            auto& slot = Slots.at(index);
            if (slot.Record || slot.Exhausted)
            {
                continue;
            }
            auto record = std::make_unique<detail::ModuleRecord>();
            const std::scoped_lock lock(AdmissionMutex);
            slot.Record = std::move(record);
            slot.Record->Handle = {
                .Domain = Domain,
                .Slot = static_cast<std::uint32_t>(index + 1),
                .Generation = slot.Generation,
            };
            slot.Record->Resident = resident;
            if (const auto* shared = std::get_if<SharedModuleCandidate>(&candidates.subspan(pending.size()).front()))
            {
                slot.Record->Descriptor.Id = shared->ExpectedId;
            }
            pending.push_back(slot.Record.get());
        }
        const auto rollback = [&](Error error) -> Result<ModuleLoadResult>
        {
            const auto originalReport = report;
            if (std::exchange(FailRollbackClose, false))
            {
                const auto first = std::ranges::find_if(
                    pending, [](const auto* record) { return record->Library && record->RegisterModule != nullptr; });
                if (first != pending.end())
                {
                    detail::ModuleManagerTestAccess::FailNextClose(*(*first)->Library);
                }
            }
            std::optional<Error> cleanup;
            for (auto* record : pending)
            {
                record->State = ModuleState::kCleanupPending;
                record->ResourcesDrained = true;
                record->OriginalError = error;
            }
            auto batch = RollbackOrder(pending);
            if (const auto released = ReleaseBatch(batch); !released)
            {
                cleanup = released.error();
                FailedBatches.push_back(std::move(batch));
                Runtime->SetCleanupBlocked(true);
            }
            report = originalReport;
            report.Handle = {};
            report.PrimaryError = error;
            report.OriginalError = error;
            report.CleanupError = cleanup;
            report.CleanupOwner = cleanup ? ModuleCleanupOwner::kManagerCleanup : ModuleCleanupOwner::kNone;
            report.Progress = cleanup ? ModuleOperationProgress::kCleanupPending : ModuleOperationProgress::kRolledBack;
            return std::unexpected(error);
        };
        for (std::size_t index = 0; index < candidates.size(); ++index)
        {
            auto& record = *pending.at(index);
            QueryModuleFn query{};
            const auto* shared = std::get_if<SharedModuleCandidate>(&candidates.subspan(index).front());
            if (shared != nullptr)
            {
                record.Descriptor.Id = shared->ExpectedId;
                auto library = detail::NativeModuleLibrary::Open(shared->Path);
                if (!library)
                {
                    return rollback(library.error());
                }
                record.Library.emplace(std::move(*library));
                const auto resolved = record.Library->ResolveQuery();
                if (!resolved)
                {
                    return rollback(resolved.error());
                }
                query = *resolved;
            }
            else
            {
                query = std::get<StaticModuleCandidate>(candidates.subspan(index).front()).Query;
            }
            if (const auto copied = detail::CopyDefinition(record, query, report); !copied)
            {
                return rollback(copied.error());
            }
            if (shared != nullptr && record.Descriptor.Id != shared->ExpectedId)
            {
                record.Descriptor.Id = shared->ExpectedId;
                record.RegisterModule = nullptr;
                return rollback(MakeModuleError(ModuleError::kInvalidDescriptor));
            }
        }
        auto resolved = detail::ResolveCatalog(existing, pending, selections, report);
        if (!resolved)
        {
            return rollback(resolved.error());
        }
        if (resident)
        {
            const auto foundation = std::ranges::find_if(pending, [](const auto* record)
                                                         { return record->Descriptor.Id == kFoundationModuleId; });
            const auto runtime = std::ranges::find_if(pending, [](const auto* record)
                                                      { return record->Descriptor.Id == kModuleRuntimeModuleId; });
            if (foundation == pending.end() || runtime == pending.end() ||
                (*foundation)->Descriptor.Version != ModuleVersion{.Major = 1, .Minor = 0, .Patch = 0} ||
                (*runtime)->Descriptor.Version != ModuleVersion{.Major = 1, .Minor = 0, .Patch = 0} ||
                !std::ranges::any_of((*runtime)->Descriptor.Dependencies,
                                     [](const auto& dependency)
                                     {
                                         return dependency.Id == kFoundationModuleId &&
                                                dependency.Version ==
                                                    ModuleVersion{.Major = 1, .Minor = 0, .Patch = 0} &&
                                                dependency.Kind == DependencyKind::kRequired;
                                     }))
            {
                return rollback(MakeModuleError(ModuleError::kInvalidResidentSet));
            }
        }
        Runtime->AttachModules(*resolved);
        ModuleLoadResult result;
        for (auto* record : *resolved)
        {
            record->Delivered = true;
            {
                const std::scoped_lock lock(AdmissionMutex);
                Slots.at(record->Handle.Slot - 1).PublishedCode = record->Code;
            }
            result.Modules.push_back(record->Handle);
        }
        report = {.Progress = ModuleOperationProgress::kLoaded, .PrimaryError = std::nullopt};
        return result;
    }
};

ModuleManager::ModuleManager(std::unique_ptr<State> state) noexcept
    : Data(std::move(state))
{
}
void detail::ModuleManagerTestAccess::FailNextNativeClose(ModuleManager& manager, ModuleHandle handle) noexcept
{
    if (auto* record = manager.Data->Resolve(handle); record != nullptr && record->Library)
    {
        FailNextClose(*record->Library);
    }
}
void detail::ModuleManagerTestAccess::FailNextRollbackClose(ModuleManager& manager) noexcept
{
    manager.Data->FailRollbackClose = true;
}
ModuleManager::ModuleManager(ModuleManager&&) noexcept = default;
ModuleManager& ModuleManager::operator=(ModuleManager&& other) noexcept
{
    if (this != &other)
    {
        const ModuleManager old(std::move(Data));
        Data = std::move(other.Data);
    }
    return *this;
}
ModuleManager::~ModuleManager()
{
    if (!Data)
    {
        return;
    }
    if (Data->Operating)
    {
        std::terminate();
    }
    ModuleOperationReport report;
    if (!BeginShutdown(report) || !TryFinishShutdown(report))
    {
        std::terminate();
    }
}
Result<ModuleManager> ModuleManager::Create(RegistrationScope& declarations, std::span<const QueryModuleFn> queries,
                                            std::span<const OptionalDependencyBinding> selections,
                                            ModuleOperationReport& report) noexcept
{
    if (queries.size() > kMaxCatalogModules || selections.size() > kMaxCatalogModules * kMaxModuleDependencies)
    {
        return Reject(report, ModuleError::kLimitExceeded);
    }
    if (queries.size() < 2)
    {
        return Reject(report, ModuleError::kInvalidResidentSet);
    }
    const auto domain = detail::AllocateScopeDomain();
    if (!domain)
    {
        return Reject(report, ModuleError::kLimitExceeded);
    }
    ModuleManager manager(std::make_unique<State>(declarations, *domain));
    std::vector<ModuleLoadCandidate> candidates;
    for (const auto query : queries)
    {
        candidates.emplace_back(StaticModuleCandidate{query});
    }
    const auto accepted = manager.Data->Accept(candidates, selections, report, true);
    if (!accepted)
    {
        return std::unexpected(accepted.error());
    }
    return manager;
}
Result<ModuleLoadResult> ModuleManager::Load(std::span<const ModuleLoadCandidate> candidates,
                                             std::span<const OptionalDependencyBinding> selections,
                                             ModuleOperationReport& report) noexcept
{
    if (!Data)
    {
        return Reject(report, ModuleError::kInvalidState);
    }
    const State::Operation operation(*Data);
    if (!operation.Accepted)
    {
        return Reject(report, ModuleError::kBusy);
    }
    if (Data->Runtime->IsShuttingDown())
    {
        return Reject(report, ModuleError::kInvalidState);
    }
    return Data->Accept(candidates, selections, report, false);
}
std::optional<ModuleHandle> ModuleManager::Find(ModuleId id) const noexcept
{
    if (Data)
    {
        for (const auto& slot : Data->Slots)
        {
            if (slot.Record && slot.Record->Delivered && slot.Record->Descriptor.Id == id)
            {
                return slot.Record->Handle;
            }
        }
    }
    return std::nullopt;
}
Result<ModuleSnapshot> ModuleManager::Inspect(ModuleHandle handle) const noexcept
{
    const auto* record = Data ? Data->Resolve(handle) : nullptr;
    if (record == nullptr || !record->Delivered)
    {
        return std::unexpected(MakeModuleError(ModuleError::kInvalidHandle));
    }
    return ModuleSnapshot{
        .Descriptor = record->Descriptor,
        .Bindings = record->Bindings,
        .State = record->State,
        .Resident = record->Resident,
    };
}
std::vector<ModuleHandle> ModuleManager::Modules() const
{
    std::vector<ModuleHandle> result;
    if (Data)
    {
        for (const auto& slot : Data->Slots)
        {
            if (slot.Record && slot.Record->Delivered)
            {
                result.push_back(slot.Record->Handle);
            }
        }
    }
    return result;
}
std::vector<ModuleHandle> ModuleManager::RegistrationOrder() const
{
    auto pending = Modules();
    std::vector<ModuleHandle> result;
    while (!pending.empty())
    {
        const auto before = pending.size();
        std::erase_if(pending,
                      [&](ModuleHandle handle)
                      {
                          const auto* record = Data->Resolve(handle);
                          const bool ready = std::ranges::all_of(
                              record->Bindings,
                              [&](const auto& binding)
                              {
                                  return !binding.IsBound() ||
                                         std::ranges::any_of(
                                             result, [&](ModuleHandle done)
                                             { return Data->Resolve(done)->Descriptor.Id == binding.Dependency; });
                              });
                          if (ready)
                          {
                              result.push_back(handle);
                          }
                          return ready;
                      });
        if (before == pending.size())
        {
            break;
        }
    }
    return result;
}

Result<ModuleCodePin> ModuleManager::TryPinForBusiness(ModuleHandle handle) const noexcept
{
    if (!Data || handle.Domain != Data->Domain || handle.Slot == 0 || handle.Slot > Data->Slots.size())
    {
        return std::unexpected(MakeModuleError(ModuleError::kInvalidHandle));
    }
    std::shared_ptr<detail::ModuleCodeControl> control;
    {
        const std::scoped_lock lock(Data->AdmissionMutex);
        const auto& slot = Data->Slots.at(handle.Slot - 1);
        if (slot.Generation != handle.Generation || !slot.PublishedCode)
        {
            return std::unexpected(MakeModuleError(ModuleError::kInvalidHandle));
        }
        control = slot.PublishedCode;
    }
    if (!control->Gate->TryEnter())
    {
        return std::unexpected(MakeModuleError(ModuleError::kNotReady));
    }
    // 闸门临时计数覆盖 ExternalPins 发布窗口；FinishRecord 先观察闸门排空再观察 Pin。
    if (control->ExternalPins.fetch_add(1, std::memory_order_relaxed) == std::numeric_limits<std::uint64_t>::max())
    {
        std::terminate();
    }
    control->Gate->Leave();
    return ModuleCodePin(std::move(control));
}

Status ModuleManager::Register(ModuleHandle handle, ModuleOperationReport& report) noexcept
{
    if (!Data)
    {
        return Reject(report, ModuleError::kInvalidState);
    }
    const State::Operation operation(*Data);
    if (!operation.Accepted || Data->Declarations->Registering)
    {
        return Reject(report, ModuleError::kBusy, handle);
    }
    auto* record = Data->Resolve(handle);
    if (record == nullptr || !record->Delivered)
    {
        return Reject(report, ModuleError::kInvalidHandle, handle);
    }
    const auto id = record->Descriptor.Id;
    if (Data->Runtime->IsShuttingDown() || record->State != ModuleState::kLoaded)
    {
        return Reject(report, ModuleError::kInvalidState, handle, id);
    }
    for (const auto& binding : record->Bindings)
    {
        if (!binding.IsBound())
        {
            continue;
        }
        const auto provider = Find(binding.Dependency);
        const auto* dependency = provider ? Data->Resolve(*provider) : nullptr;
        if (dependency == nullptr ||
            (dependency->State != ModuleState::kRegistered && dependency->State != ModuleState::kActive))
        {
            auto result = Reject(report, ModuleError::kDependencyNotRegistered, handle, id);
            report.RelatedModule = binding.Dependency;
            return result;
        }
    }
    struct Guard
    {
        RegistrationScope* Scope;
        detail::ScopeRuntime* Runtime;
        Guard(RegistrationScope* scope, detail::ScopeRuntime* runtime)
            : Scope(scope)
            , Runtime(runtime)
        {
        }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        Guard(Guard&&) = delete;
        Guard& operator=(Guard&&) = delete;
        ~Guard()
        {
            Scope->Registering = false;
            Runtime->EndOperation();
        }
    };
    if (!Data->Runtime->TryBeginOperation())
    {
        return Reject(report, ModuleError::kBusy, handle, id);
    }
    Data->Declarations->Registering = true;
    const Guard guard{Data->Declarations, Data->Runtime.get()};
    RegistrationContext transaction(*Data->Declarations, id, Data->Domain);
    const auto result = record->RegisterModule(transaction);
    if (transaction.Failure || !result)
    {
        const auto error = transaction.Failure ? *transaction.Failure : result.error();
        record->OriginalError = error;
        Data->Runtime->RegistrationFailed(id, error);
        if (record->Resident)
        {
            Data->ShutdownError = error;
            Data->Runtime->RequestShutdown();
        }
        transaction.PendingRecipes.clear();
        report = {
            .Progress = ModuleOperationProgress::kCleanupPending,
            .PrimaryError = error,
            .Module = id,
            .OriginalError = error,
            .Handle = handle,
            .CleanupOwner = record->Resident ? ModuleCleanupOwner::kManagerShutdown : ModuleCleanupOwner::kCallerUnload,
        };
        return std::unexpected(error);
    }
    auto& scope = *Data->Declarations;
    scope.Published.reserve(scope.Published.size() + transaction.Pending.size());
    scope.Recipes.reserve(scope.Recipes.size() + transaction.PendingRecipes.size());
    std::ranges::move(transaction.Pending, std::back_inserter(scope.Published));
    std::ranges::move(transaction.PendingRecipes, std::back_inserter(scope.Recipes));
    record->State = ModuleState::kRegistered;
    report = {
        .Progress = ModuleOperationProgress::kRegistered,
        .PrimaryError = std::nullopt,
        .Module = id,
        .Handle = handle,
    };
    return {};
}
Status ModuleManager::Activate(ModuleHandle handle, ModuleOperationReport& report) noexcept
{
    if (!Data)
    {
        return Reject(report, ModuleError::kInvalidState);
    }
    const State::Operation operation(*Data);
    if (!operation.Accepted)
    {
        return Reject(report, ModuleError::kBusy, handle);
    }
    auto* record = Data->Resolve(handle);
    if (record == nullptr || !record->Delivered)
    {
        return Reject(report, ModuleError::kInvalidHandle, handle);
    }
    const auto previous = record->State;
    const auto result = Data->Runtime->Activate(record->Descriptor.Id, report);
    report.Handle = handle;
    if (!result && previous == ModuleState::kRegistered && record->State == ModuleState::kCleanupPending)
    {
        record->OriginalError = result.error();
        report.CleanupOwner = ModuleCleanupOwner::kCallerUnload;
        if (record->Resident)
        {
            Data->ShutdownError = result.error();
            Data->Runtime->RequestShutdown();
            report.CleanupOwner = ModuleCleanupOwner::kManagerShutdown;
        }
    }
    return result;
}
Status ModuleManager::BeginUnload(ModuleHandle handle, ModuleOperationReport& report) noexcept
{
    if (!Data)
    {
        return Reject(report, ModuleError::kInvalidState);
    }
    const State::Operation operation(*Data);
    if (!operation.Accepted)
    {
        return Reject(report, ModuleError::kBusy, handle);
    }
    auto* record = Data->Resolve(handle);
    if (record == nullptr || !record->Delivered)
    {
        return Reject(report, ModuleError::kInvalidHandle, handle);
    }
    if (record->Resident)
    {
        return Reject(report, ModuleError::kResidentModule, handle, record->Descriptor.Id);
    }
    if (!Data->FailedBatches.empty())
    {
        return Reject(report, ModuleError::kDependencyInUse, handle, record->Descriptor.Id);
    }
    const auto result = Data->Runtime->BeginUnload(record->Descriptor.Id, report);
    report.Handle = handle;
    if (result)
    {
        report.CleanupOwner = ModuleCleanupOwner::kCallerUnload;
    }
    return result;
}
Status ModuleManager::FinishRecord(ModuleHandle handle, ModuleOperationReport& report) noexcept
{
    auto* record = Data->Resolve(handle);
    if (!Data->FailedBatches.empty())
    {
        return Reject(report, ModuleError::kBusy, handle, record->Descriptor.Id);
    }
    if (!record->ResourcesDrained)
    {
        const auto drained = Data->Runtime->TryFinishResources(handle, report);
        if (!drained)
        {
            return drained;
        }
    }
    ModuleOperationReport completed{
        .Progress = ModuleOperationProgress::kCleanupPending,
        .PrimaryError = record->OriginalError,
        .Module = record->Descriptor.Id,
        .OriginalError = record->OriginalError,
        .Handle = record->Delivered ? handle : ModuleHandle(),
        .CleanupOwner =
            Data->Runtime->IsShuttingDown() ? ModuleCleanupOwner::kManagerShutdown : ModuleCleanupOwner::kCallerUnload,
    };
    // 资源销毁不等待外部代码 Pin；原生释放必须等待接纳中的短暂借用与外部 Pin。
    if (record->Code->HasExternalUsers())
    {
        completed.CleanupError = MakeModuleError(ModuleError::kBusy);
        if (!completed.PrimaryError)
        {
            completed.PrimaryError = completed.CleanupError;
        }
        report = completed;
        return std::unexpected(*completed.CleanupError);
    }
    record->Metadata = nullptr;
    if (record->Resident)
    {
        report = completed;
        return {};
    }
    if (record->Library)
    {
        const auto released = record->Library->Release();
        if (!released)
        {
            completed.CleanupError = released.error();
            if (!completed.PrimaryError)
            {
                completed.PrimaryError = released.error();
            }
            report = completed;
            return released;
        }
        record->Library.reset();
    }
    Data->Retire(handle);
    completed.Progress = ModuleOperationProgress::kUnloaded;
    completed.CleanupOwner = ModuleCleanupOwner::kNone;
    // 原生释放可能同步重入并复用 report；在所有外部代码返回后完整发布。
    report = completed;
    return {};
}
Status ModuleManager::TryFinishUnload(ModuleHandle handle, ModuleOperationReport& report) noexcept
{
    if (!Data)
    {
        return Reject(report, ModuleError::kInvalidState);
    }
    const State::Operation operation(*Data);
    if (!operation.Accepted)
    {
        return Reject(report, ModuleError::kBusy, handle);
    }
    const auto* record = Data->Resolve(handle);
    if (record == nullptr || !record->Delivered)
    {
        return Reject(report, ModuleError::kInvalidHandle, handle);
    }
    if (record->Resident)
    {
        return Reject(report, ModuleError::kResidentModule, handle, record->Descriptor.Id);
    }
    if (record->State != ModuleState::kCleanupPending)
    {
        return Reject(report, ModuleError::kInvalidState, handle, record->Descriptor.Id);
    }
    const auto result = FinishRecord(handle, report);
    if (!result)
    {
        report.Handle = handle;
        report.CleanupOwner = ModuleCleanupOwner::kCallerUnload;
    }
    return result;
}
ScopeHandle ModuleManager::HostScope() const noexcept { return Data ? Data->Runtime->HostScope() : ScopeHandle{}; }
Result<ModuleMetaBorrow> ModuleManager::AcquireMetadata(ModuleId owner, const std::type_info& type) const noexcept
{
    if (Data && Data->Operating)
    {
        return std::unexpected(MakeModuleError(ModuleError::kBusy));
    }
    if (!Data || Data->Closed || Data->Runtime->IsShuttingDown())
    {
        return std::unexpected(MakeModuleError(ModuleError::kInvalidState));
    }
    const auto handle = Find(owner);
    if (!handle)
    {
        return std::unexpected(MakeModuleError(ModuleError::kNotFound));
    }
    const auto* record = Data->Resolve(*handle);
    if (record->State != ModuleState::kActive)
    {
        return std::unexpected(MakeModuleError(ModuleError::kNotReady));
    }
    auto pin = TryPinForBusiness(*handle);
    if (!pin)
    {
        return std::unexpected(pin.error());
    }
    if (record->Metadata != nullptr)
    {
        for (const auto& entry : record->Metadata->Entries)
        {
            if (*entry.CppType == type)
            {
                return ModuleMetaBorrow(std::move(*pin), entry.Info);
            }
        }
    }
    return std::unexpected(MakeModuleError(ModuleError::kNotFound));
}
Result<std::shared_ptr<detail::ScopeObjectControl>>
ModuleManager::AcquireScopeResource(const detail::ScopeResourceQuery& query, const std::type_info& type) const noexcept
{
    if (Data && Data->Operating)
    {
        return std::unexpected(MakeModuleError(ModuleError::kBusy));
    }
    if (!Data || Data->Closed || Data->Runtime->IsShuttingDown())
    {
        return std::unexpected(MakeModuleError(ModuleError::kInvalidState));
    }
    return Data->Runtime->AcquireScopeResource(query, type);
}
Result<ScopeHandle> ModuleManager::CreateScope(pg::service::Scope kind, ScopeHandle parent,
                                               ModuleOperationReport& report) noexcept
{
    if (!Data)
    {
        return Reject(report, ModuleError::kInvalidState);
    }
    const State::Operation operation(*Data);
    if (!operation.Accepted)
    {
        return Reject(report, ModuleError::kBusy);
    }
    auto result = Data->Runtime->CreateScope(kind, parent, report);
    CompleteScopeReport(report, ModuleCleanupOwner::kManagerCleanup, Data->Runtime->IsShuttingDown());
    return result;
}
Status ModuleManager::BeginCloseScope(ScopeHandle scope, ModuleOperationReport& report) noexcept
{
    if (!Data)
    {
        return Reject(report, ModuleError::kInvalidState);
    }
    const State::Operation operation(*Data);
    if (!operation.Accepted)
    {
        return Reject(report, ModuleError::kBusy);
    }
    const auto result = Data->Runtime->BeginCloseScope(scope, report);
    CompleteScopeReport(report, ModuleCleanupOwner::kCallerUnload, Data->Runtime->IsShuttingDown());
    return result;
}
Status ModuleManager::TryFinishCloseScope(ScopeHandle scope, ModuleOperationReport& report) noexcept
{
    if (!Data)
    {
        return Reject(report, ModuleError::kInvalidState);
    }
    const State::Operation operation(*Data);
    if (!operation.Accepted)
    {
        return Reject(report, ModuleError::kBusy);
    }
    if (!Data->FailedBatches.empty())
    {
        return Reject(report, ModuleError::kBusy);
    }
    const auto result = Data->Runtime->TryFinishCloseScope(scope, report);
    CompleteScopeReport(report, ModuleCleanupOwner::kCallerUnload, Data->Runtime->IsShuttingDown());
    return result;
}
Status ModuleManager::Cleanup(ModuleOperationReport& report, bool confirmShutdown) noexcept
{
    std::optional<Error> pending;
    // 逐记录清理也可能回收未交付的失败 Scope，必须先拥有其原始失败值。
    const auto runtimeFailure = Data->Runtime->PendingFailure();
    auto original = Data->ShutdownError;
    for (const auto* record : Data->Records())
    {
        if (!original && record->OriginalError)
        {
            original = record->OriginalError;
        }
    }
    if (!original)
    {
        original = runtimeFailure;
    }
    // 不完整 Query 不能提供可信的 Provider 集合；保守保留已有资源，业务门保持原状。
    for (auto batch = Data->FailedBatches.begin(); batch != Data->FailedBatches.end();)
    {
        const auto released = Data->ReleaseBatch(*batch);
        if (!released)
        {
            report = {
                .Progress = ModuleOperationProgress::kCleanupPending,
                .PrimaryError = original ? original : std::optional(released.error()),
                .OriginalError = original,
                .CleanupError = released.error(),
                .CleanupOwner = Data->Runtime->IsShuttingDown() ? ModuleCleanupOwner::kManagerShutdown
                                                                : ModuleCleanupOwner::kManagerCleanup,
            };
            return released;
        }
        batch = Data->FailedBatches.erase(batch);
    }
    Data->Runtime->SetCleanupBlocked(false);
    // Runtime 按有效依赖拒绝尚不可清理的 Provider；多轮只在有最终确认进展时继续。
    std::vector<ModuleHandle> nativeFailures;
    std::optional<Error> nativeError;
    bool progress = true;
    while (progress)
    {
        progress = false;
        pending = nativeError;
        const auto current = Data->Records();
        const bool businessPending = std::ranges::any_of(current, [](const auto* record) { return !record->Resident; });
        for (auto iter = current.rbegin(); iter != current.rend(); ++iter)
        {
            auto* record = *iter;
            if (record->Resident && businessPending && Data->Runtime->IsShuttingDown() &&
                !Data->Runtime->HasBusinessDependency(record->Handle))
            {
                continue;
            }
            if (record->State != ModuleState::kCleanupPending ||
                (record->Resident && record->ResourcesDrained && !record->Code->HasExternalUsers()))
            {
                continue;
            }
            if (std::ranges::find(nativeFailures, record->Handle) != nativeFailures.end())
            {
                continue;
            }
            const bool resident = record->Resident;
            const auto result = FinishRecord(record->Handle, report);
            if (!result)
            {
                if (record->ResourcesDrained && record->Library)
                {
                    nativeFailures.push_back(record->Handle);
                    nativeError = result.error();
                }
                if (!pending || (*pending == MakeModuleError(ModuleError::kBusy) && result.error() != *pending))
                {
                    pending = result.error();
                }
            }
            else
            {
                progress = true;
                if (resident)
                {
                    record->ResourcesDrained = true;
                }
            }
        }
    }
    ModuleOperationReport scopeReport;
    const auto scopes = Data->Runtime->PollCleanup(scopeReport);
    if (!original)
    {
        original = scopeReport.OriginalError;
    }
    if (!scopes && !pending && !Data->Runtime->IsShuttingDown())
    {
        pending = scopes.error();
    }
    if (!pending && confirmShutdown)
    {
        const auto finished = Data->Runtime->TryFinishShutdown(scopeReport);
        if (!finished)
        {
            pending = finished.error();
        }
        else
        {
            for (auto* record : Data->Records())
            {
                Data->Retire(record->Handle);
            }
            Data->Closed = true;
        }
    }
    report = {
        .Progress = pending
                        ? ModuleOperationProgress::kCleanupPending
                        : (confirmShutdown ? ModuleOperationProgress::kUnloaded : ModuleOperationProgress::kUnchanged),
        .PrimaryError = original ? original : pending,
        .OriginalError = original,
        .CleanupError = pending,
        .CleanupOwner = Data->Runtime->IsShuttingDown()
                            ? ModuleCleanupOwner::kManagerShutdown
                            : (pending ? ModuleCleanupOwner::kManagerCleanup : ModuleCleanupOwner::kNone),
    };
    if (pending)
    {
        return std::unexpected(*pending);
    }
    return {};
}
Status ModuleManager::PollCleanup(ModuleOperationReport& report) noexcept
{
    if (!Data)
    {
        return Reject(report, ModuleError::kInvalidState);
    }
    const State::Operation operation(*Data);
    if (!operation.Accepted)
    {
        return Reject(report, ModuleError::kBusy);
    }
    return Cleanup(report, false);
}
Status ModuleManager::BeginShutdown(ModuleOperationReport& report) noexcept
{
    if (!Data)
    {
        return Reject(report, ModuleError::kInvalidState);
    }
    const State::Operation operation(*Data);
    if (!operation.Accepted)
    {
        return Reject(report, ModuleError::kBusy);
    }
    const auto result = Data->Runtime->BeginShutdown(report);
    report.CleanupOwner = ModuleCleanupOwner::kManagerShutdown;
    if (!Data->ShutdownError)
    {
        Data->ShutdownError = report.OriginalError;
        for (const auto* record : Data->Records())
        {
            if (!Data->ShutdownError && record->OriginalError)
            {
                Data->ShutdownError = record->OriginalError;
            }
        }
    }
    report.OriginalError = Data->ShutdownError;
    if (Data->ShutdownError)
    {
        report.PrimaryError = Data->ShutdownError;
    }
    return result;
}
Status ModuleManager::TryFinishShutdown(ModuleOperationReport& report) noexcept
{
    if (!Data)
    {
        return Reject(report, ModuleError::kInvalidState);
    }
    const State::Operation operation(*Data);
    if (!operation.Accepted)
    {
        return Reject(report, ModuleError::kBusy);
    }
    if (!Data->Runtime->IsShuttingDown())
    {
        return Reject(report, ModuleError::kInvalidState);
    }
    return Cleanup(report, true);
}
} // namespace pg::module
