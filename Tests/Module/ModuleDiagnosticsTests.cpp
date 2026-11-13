#include "FailureReport.h"
#include "Fixtures/ModuleDiagnosticsFixture.h"
#include "LogRecord.h"
#include "LogTypes.h"
#include "ModuleBuiltin.h"
#include "ModuleDescriptor.h"
#include "ModuleDiagnostics.h"
#include "ModuleError.h"
#include "ModuleManager.h"
#include "ModuleOperation.h"
#include "RegistrationContext.h"
#include "ScopeIdentity.h"
#include <FoundationModule.h>
#include <LogModule.h>
#include <LogQueue.h>
#include <ModuleDiagnosticsInternal.h>
#include <WriterControl.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <gtest/gtest.h>
#include <print>
#include <vector>
namespace
{
using namespace pg;
using namespace pg::module;
using namespace pg::test::module_diagnostics;
TEST(ModuleDiagnostics, CoreIncludesLogAndBothRequiredEdges)
{
    EXPECT_EQ(BuiltinResidentQueries().size(), 3U);
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    for (const auto id : {kModuleRuntimeModuleId, log::kLogModuleId})
    {
        const auto handle = manager->Find(id);
        ASSERT_TRUE(handle);
        const auto snapshot = manager->Inspect(*handle);
        ASSERT_TRUE(snapshot);
        EXPECT_TRUE(std::ranges::any_of(snapshot->Bindings,
                                        [](const auto& binding)
                                        {
                                            return binding.Dependency == kFoundationModuleId &&
                                                   binding.Reason == DependencyBindingReason::kRequired;
                                        }));
        if (id == kModuleRuntimeModuleId)
        {
            EXPECT_TRUE(std::ranges::any_of(snapshot->Bindings,
                                            [](const auto& binding)
                                            {
                                                return binding.Dependency == log::kLogModuleId &&
                                                       binding.Reason == DependencyBindingReason::kRequired;
                                            }));
        }
    }
    std::println("Task9 sizeof: ModuleOperationReport={} FailureReport={} FailureSite={}",
                 sizeof(ModuleOperationReport), sizeof(FailureReport), sizeof(FailureSite));
    std::println("Task9 cache: HistoryEntry={} PollEntry={} persistentReports={}",
                 sizeof(pg::module::detail::ModuleHistoryDiagnostic), sizeof(pg::module::detail::ModulePollDiagnostic),
                 pg::module::detail::kModuleDiagnosticReportBytes);
    EXPECT_LE(sizeof(ModuleOperationReport), 4096U);
}
TEST(ModuleDiagnostics, RegisterAndStartOriginSurvivesRollback)
{
    for (const auto origin :
         {ModuleFailureOrigin::kRegister, ModuleFailureOrigin::kFactory, ModuleFailureOrigin::kStart})
    {
        Controls controls;
        controls.FailRegister = origin == ModuleFailureOrigin::kRegister;
        controls.FailFactory = origin == ModuleFailureOrigin::kFactory;
        controls.FailStart = origin == ModuleFailureOrigin::kStart;
        RegistrationScope declarations;
        ModuleOperationReport report;
        auto manager = Create(declarations, report);
        ASSERT_TRUE(manager);
        controls.Reenter = true;
        controls.Manager = &*manager;
        controls.Destination = &report;
        const auto handle = manager->Find(kOwner).value_or(ModuleHandle{});
        const auto registered = manager->Register(handle, report);
        const auto failed = controls.FailRegister ? registered : manager->Activate(handle, report);
        ASSERT_FALSE(failed);
        EXPECT_EQ(failed.error(), kOriginal);
        EXPECT_EQ(report.OriginalError, kOriginal);
        EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kManagerShutdown);
        EXPECT_EQ(report.FailureOrigin, origin);
        const auto site = report.Failure.PrimarySite();
        ASSERT_TRUE(site);
        EXPECT_EQ(site->Cause, kOriginal);
        EXPECT_EQ(site->Line(), controls.OriginLine);
        EXPECT_TRUE(site->File().ends_with("ModuleDiagnosticsFixture.h"));
        const auto copied = report;
        ASSERT_TRUE(manager->TryFinishShutdown(report));
        ASSERT_TRUE(report.Failure.PrimarySite());
        EXPECT_EQ(report.Failure.PrimarySite()->Line(), controls.OriginLine);
        EXPECT_EQ(copied.Failure.PrimarySite()->Message(), "owned callback origin");
    }
}
TEST(ModuleDiagnostics, CallbackContextCannotOverrideReturnedError)
{
    Controls controls;
    controls.FailRegister = true;
    controls.Mismatch = true;
    controls.Reenter = true;
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = Create(declarations, report);
    ASSERT_TRUE(manager);
    controls.Manager = &*manager;
    controls.Destination = &report;
    const auto result = manager->Register(manager->Find(kOwner).value_or(ModuleHandle{}), report);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), kOriginal);
    EXPECT_EQ(report.PrimaryError, kOriginal);
    EXPECT_EQ(report.FailureOrigin, ModuleFailureOrigin::kUnknown);
    const auto primary = report.Failure.PrimarySite();
    ASSERT_TRUE(primary);
    EXPECT_EQ(primary->Cause, kOriginal);
    EXPECT_FALSE(primary->OriginKnown());
    const auto secondary = report.Failure.SecondarySite(0);
    ASSERT_TRUE(secondary);
    EXPECT_EQ(secondary->Cause, kMismatch);
    EXPECT_EQ(secondary->Line(), controls.OriginLine);
    EXPECT_EQ(report.Failure.ContextValue(1), "alive after reentry");
    EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kManagerShutdown);
}
TEST(ModuleDiagnostics, PollErrorKeepsOriginalCause)
{
    Controls controls;
    controls.FailStart = true;
    controls.PollError = true;
    controls.Done = false;
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = Create(declarations, report);
    ASSERT_TRUE(manager);
    const auto handle = manager->Find(kOwner).value_or(ModuleHandle{});
    ASSERT_TRUE(manager->Register(handle, report));
    ASSERT_FALSE(manager->Activate(handle, report));
    EXPECT_EQ(report.OriginalError, kOriginal);
    EXPECT_EQ(report.CleanupError, kCleanup);
    const auto failed = manager->TryFinishShutdown(report);
    EXPECT_FALSE(failed);
    EXPECT_EQ(failed.error(), kCleanup);
    EXPECT_EQ(report.PrimaryError, kOriginal);
    ASSERT_TRUE(report.Failure.PrimarySite());
    EXPECT_EQ(report.Failure.PrimarySite()->Line(), controls.OriginLine);
    bool found{};
    for (std::size_t index = 0; index < report.Failure.EntryCount(); ++index)
    {
        const auto secondary = report.Failure.SecondarySite(index);
        found |= secondary && secondary->Cause == kCleanup && secondary->OriginKnown();
    }
    EXPECT_TRUE(found);
    EXPECT_EQ(report.CleanupOwner, ModuleCleanupOwner::kManagerShutdown);
    controls.Done = true;
    EXPECT_TRUE(manager->TryFinishShutdown(report));
    EXPECT_EQ(report.OriginalError, kOriginal);
}

TEST(ModuleDiagnostics, SameErrorCleanupSiteSurvivesFullPollCache)
{
    Controls controls;
    controls.ResourceCount = 12;
    controls.FailStart = true;
    controls.FailStartAt = 11;
    controls.Done = false;
    controls.PollError = true;
    controls.FailPollAt = 0;
    controls.PollUsesOriginalError = true;
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = Create(declarations, report);
    ASSERT_TRUE(manager);
    const auto handle = manager->Find(kOwner).value_or(ModuleHandle{});
    ASSERT_TRUE(manager->Register(handle, report));
    const auto failed = manager->Activate(handle, report);
    EXPECT_FALSE(failed);
    EXPECT_EQ(report.PrimaryError, kOriginal);
    EXPECT_EQ(report.OriginalError, kOriginal);
    EXPECT_EQ(report.CleanupError, kOriginal);
    EXPECT_EQ(report.FailureOrigin, ModuleFailureOrigin::kStart);
    EXPECT_TRUE(report.WaitReasonsTruncated);
    const auto primary = report.Failure.PrimarySite();
    ASSERT_TRUE(primary);
    EXPECT_EQ(primary->Message(), "owned callback origin");
    bool found{};
    for (std::size_t index = 0; index < report.Failure.EntryCount(); ++index)
    {
        const auto site = report.Failure.SecondarySite(index);
        found |= site && site->Cause == kOriginal && site->Message() == "poll stopped failure" && site->OriginKnown() &&
                 site->Line() != primary->Line();
    }
    EXPECT_TRUE(found);
    controls.Done = true;
    EXPECT_TRUE(manager->TryFinishShutdown(report));
}

TEST(ModuleDiagnostics, WriterCarriesCopiedModuleScopeAndDeclarationTags)
{
    log::detail::LogQueue queue;
    ASSERT_TRUE(queue.Initialize(8, 1));
    log::detail::WriterControlOwner writerOwner;
    ASSERT_TRUE(writerOwner.Initialize(queue, log::Level::kInfo));
    Controls controls;
    controls.Emit = true;
    RegistrationScope declarations;
    ModuleOperationReport report;
    std::vector<QueryModuleFn> queries(BuiltinResidentQueries().begin(), BuiltinResidentQueries().end());
    queries.push_back(Query);
    auto manager = ModuleManager::Create(declarations, queries, {}, report, writerOwner.GetWriter());
    ASSERT_TRUE(manager);
    const auto handle = manager->Find(kOwner).value_or(ModuleHandle{});
    ASSERT_TRUE(manager->Register(handle, report));
    ASSERT_TRUE(manager->Activate(handle, report));
    EXPECT_EQ(controls.Submissions,
              (std::vector{log::SubmitResult::kAccepted, log::SubmitResult::kAccepted, log::SubmitResult::kAccepted}));
    log::LogRecord record;
    ASSERT_TRUE(queue.TryPop(record));
    EXPECT_EQ(record.Message(), "register");
    EXPECT_EQ(record.Tags.Module, kOwner);
    EXPECT_FALSE(record.Tags.Scope);
    for (const auto* const message : {"factory", "start"})
    {
        ASSERT_TRUE(queue.TryPop(record));
        EXPECT_EQ(record.Message(), message);
        EXPECT_EQ(record.Tags.Module, kOwner);
        ASSERT_TRUE(record.Tags.Scope);
        EXPECT_EQ(record.Tags.Scope->Domain, manager->HostScope().Domain);
        EXPECT_EQ(record.Tags.Scope->Slot, manager->HostScope().Slot);
        ASSERT_TRUE(record.Tags.Declaration);
        EXPECT_EQ(record.Tags.Declaration->front(), std::byte{0x9e});
    }
}

TEST(ModuleDiagnostics, EmptyWriterKeepsContextLoggingDisabled)
{
    Controls controls;
    controls.Emit = true;
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = Create(declarations, report);
    ASSERT_TRUE(manager);
    const auto handle = manager->Find(kOwner).value_or(ModuleHandle{});
    ASSERT_TRUE(manager->Register(handle, report));
    ASSERT_TRUE(manager->Activate(handle, report));
    EXPECT_EQ(controls.Submissions,
              (std::vector{log::SubmitResult::kDisabled, log::SubmitResult::kDisabled, log::SubmitResult::kDisabled}));
}

const ModuleDefinition* WrongLogVersion() noexcept
{
    static const ModuleDefinition kDefinition = []
    {
        auto definition = *log::QueryLogModule();
        definition.Descriptor.Version = {.Major = 2, .Minor = 0, .Patch = 0};
        return definition;
    }();
    return &kDefinition;
}

TEST(ModuleDiagnostics, MissingCoreAndWrongLogVersionRemainRejected)
{
    for (std::size_t omitted = 0; omitted < 3; ++omitted)
    {
        RegistrationScope declarations;
        ModuleOperationReport report;
        std::vector<QueryModuleFn> queries;
        for (std::size_t index = 0; index < BuiltinResidentQueries().size(); ++index)
        {
            if (index != omitted)
            {
                queries.push_back(BuiltinResidentQueries()[index]);
            }
        }
        EXPECT_FALSE(ModuleManager::Create(declarations, queries, {}, report));
    }
    RegistrationScope declarations;
    ModuleOperationReport report;
    const std::array queries{QueryFoundationModule, WrongLogVersion, QueryModuleRuntime};
    const auto manager = ModuleManager::Create(declarations, queries, {}, report);
    ASSERT_FALSE(manager);
    EXPECT_EQ(manager.error(), MakeModuleError(ModuleError::kVersionMismatch));
}

TEST(ModuleDiagnostics, HistoryCapacityPreservesCurrentAndDoesNotBorrowAnotherScopeOrigin)
{
    Controls controls;
    controls.Kind = service::Scope::kContext;
    controls.Done = false;
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = Create(declarations, report);
    ASSERT_TRUE(manager);
    const auto owner = manager->Find(kOwner).value_or(ModuleHandle{});
    ASSERT_TRUE(manager->Register(owner, report));
    ASSERT_TRUE(manager->Activate(owner, report));
    controls.FailStart = true;
    ScopeHandle ninth;
    for (unsigned index = 0; index < 9; ++index)
    {
        const auto result = manager->CreateScope(service::Scope::kContext, manager->HostScope(), report);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), kOriginal);
        ASSERT_TRUE(report.Failure.PrimarySite());
        EXPECT_TRUE(report.Failure.PrimarySite()->OriginKnown());
        EXPECT_EQ(report.FailureHistoryTruncated, index == 8);
        ninth = report.Scope;
    }
    controls.ReadyThrough = 8;
    EXPECT_FALSE(manager->PollCleanup(report));
    EXPECT_FALSE(manager->PollCleanup(report));
    EXPECT_EQ(report.OriginalError, kOriginal);
    EXPECT_EQ(report.Scope, ninth);
    EXPECT_TRUE(report.FailureHistoryTruncated);
    ASSERT_TRUE(report.Failure.PrimarySite());
    EXPECT_FALSE(report.Failure.PrimarySite()->OriginKnown());
    // 回收的历史槽位可复用；同一个 Error 的新 Scope 不能补到第九个 Scope 的现场。
    controls.FailStart = false;
    for (unsigned index = 0; index < 8; ++index)
    {
        ASSERT_TRUE(manager->CreateScope(service::Scope::kContext, manager->HostScope(), report));
    }
    controls.FailStart = true;
    ASSERT_FALSE(manager->CreateScope(service::Scope::kContext, manager->HostScope(), report));
    const auto tenth = report.Scope;
    EXPECT_NE(tenth, ninth);
    EXPECT_FALSE(report.FailureHistoryTruncated);
    EXPECT_FALSE(manager->PollCleanup(report));
    EXPECT_EQ(report.Scope, ninth);
    EXPECT_TRUE(report.FailureHistoryTruncated);
    ASSERT_TRUE(report.Failure.PrimarySite());
    EXPECT_FALSE(report.Failure.PrimarySite()->OriginKnown());
    controls.Done = true;
    EXPECT_TRUE(manager->BeginShutdown(report));
    EXPECT_TRUE(manager->TryFinishShutdown(report));
}

TEST(ModuleDiagnostics, MissingHistoryCannotPromoteSameErrorPollToOriginalOrigin)
{
    Controls controls;
    controls.Kind = service::Scope::kContext;
    controls.Done = false;
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = Create(declarations, report);
    ASSERT_TRUE(manager);
    const auto owner = manager->Find(kOwner).value_or(ModuleHandle{});
    ASSERT_TRUE(manager->Register(owner, report));
    ASSERT_TRUE(manager->Activate(owner, report));
    controls.FailStart = true;
    ScopeHandle ninth;
    for (unsigned index = 0; index < 9; ++index)
    {
        ASSERT_FALSE(manager->CreateScope(service::Scope::kContext, manager->HostScope(), report));
        ninth = report.Scope;
    }
    controls.ReadyThrough = 8;
    EXPECT_FALSE(manager->PollCleanup(report));
    controls.PollError = true;
    controls.PollUsesOriginalError = true;
    const auto pending = manager->PollCleanup(report);
    EXPECT_FALSE(pending);
    if (!pending)
    {
        EXPECT_EQ(pending.error(), kOriginal);
    }
    EXPECT_EQ(report.OriginalError, kOriginal);
    EXPECT_EQ(report.Scope, ninth);
    EXPECT_EQ(report.FailureOrigin, ModuleFailureOrigin::kUnknown);
    EXPECT_TRUE(report.FailureHistoryTruncated);
    const auto primary = report.Failure.PrimarySite();
    EXPECT_TRUE(primary);
    if (primary)
    {
        EXPECT_FALSE(primary->OriginKnown());
    }
    bool pollSite{};
    for (std::size_t index = 0; index < report.Failure.EntryCount(); ++index)
    {
        const auto site = report.Failure.SecondarySite(index);
        pollSite |= site && site->Cause == kOriginal && site->Message() == "poll stopped failure";
    }
    EXPECT_TRUE(pollSite);
    controls.Done = true;
    EXPECT_TRUE(manager->BeginShutdown(report));
    EXPECT_TRUE(manager->TryFinishShutdown(report));
}
} // namespace
