#include <Error.h>
#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <ModuleIdentity.h>
#include <RegistrationContext.h>
#include <Result.h>
#include <StaticModuleHost.h>

#include <array>
#include <cstddef>
#include <expected>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace
{
using namespace pg::module;
constexpr ModuleId kFirst{.Bytes = {std::byte{1}}};
constexpr ModuleId kSecond{.Bytes = {std::byte{2}}};
constexpr DeclarationId kDeclarationFirst{.Bytes = {std::byte{1}}};
constexpr DeclarationId kDeclarationSecond{.Bytes = {std::byte{2}}};
constexpr pg::Error kOriginal{pg::ErrorDomainId::Constant<0x746573742e726567ULL>(), 91,
                              pg::NativeErrorCode{.Source = pg::NativeErrorSourceId{17}, .Value = 123}};

// Serial callback/Query fixture, not a production global registry.
struct Fixture
{
    inline static const ModuleDefinition* gFirst{};
    inline static const ModuleDefinition* gSecond{};
    inline static const RegistrationScope* gScope{};
    inline static StaticModuleHost* gNestedHost{};
    inline static std::string_view gName;
    inline static bool gObservedAtomic{};
    inline static bool gObservedBusy{};
    inline static bool gFailAfterNested{};
    inline static ModuleOperationReport* gReport{};
    inline static int gCalls{};
    static const ModuleDefinition* QueryFirst() noexcept { return gFirst; }
    static const ModuleDefinition* QuerySecond() noexcept { return gSecond; }
    static pg::Status DeclareFirst(RegistrationContext& context) noexcept
    {
        ++gCalls;
        const auto before = gScope->Declarations().size();
        const auto declared = context.Declare(kDeclarationFirst, gName);
        gObservedAtomic = gScope->Declarations().size() == before;
        return declared;
    }
    static pg::Status Reenter(RegistrationContext& context) noexcept
    {
        const auto nested = gNestedHost->Register(kSecond, *gReport);
        gObservedBusy = !nested && nested.error() == MakeModuleError(ModuleError::kBusy) &&
                        gReport->Progress == ModuleOperationProgress::kUnchanged;
        if (gFailAfterNested)
        {
            return std::unexpected(kOriginal);
        }
        return context.Declare(kDeclarationFirst, "outer");
    }
};

constexpr std::array kEntries{StaticCatalogEntry{Fixture::QueryFirst}, StaticCatalogEntry{Fixture::QuerySecond}};

ModuleDefinition Definition(ModuleId id, RegisterModuleFn callback, std::span<const ModuleDependency> dependencies = {})
{
    return {
        .Descriptor = {.Id = id, .Dependencies = dependencies, .DisplayName = {}, .BuildIdentity = {}},
        .RegisterModule = callback,
    };
}

pg::Status DeclareSecond(RegistrationContext& context) noexcept
{
    return context.Declare(kDeclarationSecond, "second");
}

bool RegistersOnceInDependencyOrderAndPublishesAtomically()
{
    RegistrationScope scope;
    Fixture::gScope = &scope;
    Fixture::gCalls = 0;
    Fixture::gName = "first";
    const std::array dependencies{ModuleDependency{.Id = kSecond}};
    const auto first = Definition(kFirst, Fixture::DeclareFirst, dependencies);
    const auto second = Definition(kSecond, DeclareSecond);
    Fixture::gFirst = &first;
    Fixture::gSecond = &second;
    ModuleOperationReport report;
    auto host = StaticModuleHost::Create({kEntries}, scope, {}, report);
    if (!host)
    {
        return false;
    }
    const auto early = host->Register(kFirst, report);
    if (early || early.error() != MakeModuleError(ModuleError::kDependencyNotRegistered) || Fixture::gCalls != 0 ||
        report.Progress != ModuleOperationProgress::kUnchanged || report.RelatedModule != kSecond ||
        host->Modules().front().State != StaticModuleState::kLoaded)
    {
        return false;
    }
    for (const auto id : host->RegistrationOrder())
    {
        if (!host->Register(id, report) || report.PrimaryError ||
            report.Progress != ModuleOperationProgress::kRegistered)
        {
            return false;
        }
    }
    const auto repeated = host->Register(kFirst, report);
    return !repeated && repeated.error() == MakeModuleError(ModuleError::kInvalidState) && Fixture::gCalls == 1 &&
           Fixture::gObservedAtomic && scope.Declarations().size() == 2 &&
           scope.Declarations().front().Owner == kSecond && scope.Declarations().back().Owner == kFirst &&
           scope.Declarations().back().DisplayName == "first" && report.Progress == ModuleOperationProgress::kUnchanged;
}

bool RollsBackAndPreservesOriginalErrorAndProgress()
{
    RegistrationScope scope;
    const auto failing = [](RegistrationContext& context) noexcept -> pg::Status
    {
        const auto staged = context.Declare(kDeclarationFirst, "pending");
        if (!staged)
        {
            return staged;
        }
        return std::unexpected(kOriginal);
    };
    const auto first = Definition(kFirst, failing);
    const auto second = Definition(kSecond, DeclareSecond);
    Fixture::gFirst = &first;
    Fixture::gSecond = &second;
    ModuleOperationReport report;
    auto host = StaticModuleHost::Create({kEntries}, scope, {}, report);
    if (!host || !host->Register(kSecond, report))
    {
        return false;
    }
    const auto failed = host->Register(kFirst, report);
    const auto snapshot = report;
    const auto retry = host->Register(kFirst, report);
    if (failed || failed.error() != kOriginal || snapshot.PrimaryError != kOriginal ||
        snapshot.Progress != ModuleOperationProgress::kRolledBack ||
        host->Modules().front().State != StaticModuleState::kRegistrationFailed || retry ||
        report.Progress != ModuleOperationProgress::kUnchanged || scope.Declarations().size() != 1 ||
        scope.Declarations().front().Owner != kSecond)
    {
        return false;
    }
    // Equal Error values must not erase whether the callback was entered.
    RegistrationScope otherScope;
    const auto identicalError = [](RegistrationContext&) noexcept -> pg::Status
    { return std::unexpected(MakeModuleError(ModuleError::kNotFound)); };
    const auto other = Definition(kFirst, identicalError);
    Fixture::gFirst = &other;
    auto otherHost = StaticModuleHost::Create({std::span(kEntries).first<1>()}, otherScope, {}, report);
    if (!otherHost)
    {
        return false;
    }
    const auto absent = otherHost->Register(kSecond, report);
    const auto before = report;
    const auto callback = otherHost->Register(kFirst, report);
    return !absent && !callback && absent.error() == callback.error() &&
           before.Progress == ModuleOperationProgress::kUnchanged &&
           report.Progress == ModuleOperationProgress::kRolledBack;
}

bool IgnoredDeclarationFailureCannotCommit()
{
    for (const bool existing : {false, true})
    {
        RegistrationScope scope;
        const auto duplicate = [](RegistrationContext& context) noexcept -> pg::Status
        {
            const auto first = context.Declare(kDeclarationFirst, "pending");
            if (!first)
            {
                return first;
            }
            // Intentionally misbehaving callback: production must retain this error.
            [[maybe_unused]] const auto ignoredConflict = context.Declare(kDeclarationSecond, "conflict");
            [[maybe_unused]] const auto ignoredDuplicate = context.Declare(kDeclarationSecond, "duplicate pending");
            return {};
        };
        const auto first = Definition(kFirst, duplicate);
        const auto second = Definition(kSecond, DeclareSecond);
        Fixture::gFirst = &first;
        Fixture::gSecond = &second;
        ModuleOperationReport report;
        auto host = StaticModuleHost::Create({kEntries}, scope, {}, report);
        if (!host || (existing && !host->Register(kSecond, report)))
        {
            return false;
        }
        const auto result = host->Register(kFirst, report);
        if (result || result.error() != MakeModuleError(ModuleError::kDeclarationConflict) ||
            scope.Declarations().size() != (existing ? 1U : 0U) ||
            report.Progress != ModuleOperationProgress::kRolledBack)
        {
            return false;
        }
    }
    return true;
}

bool RejectsInvalidAndOversizedDeclarations()
{
    const std::array<RegisterModuleFn, 3> callbacks{
        [](RegistrationContext& context) noexcept -> pg::Status { return context.Declare(DeclarationId(), "zero"); },
        [](RegistrationContext& context) noexcept -> pg::Status
        { return context.Declare(kDeclarationFirst, std::string(257, 'x')); },
        [](RegistrationContext& context) noexcept -> pg::Status
        {
            for (unsigned int index = 1; index <= 257; ++index)
            {
                const DeclarationId id{
                    .Bytes = {static_cast<std::byte>(index & 255U), static_cast<std::byte>(index >> 8U)},
                };
                const auto staged = context.Declare(id, "bounded");
                if (!staged)
                {
                    return staged;
                }
            }
            return {};
        },
    };
    for (std::size_t index = 0; index < callbacks.size(); ++index)
    {
        RegistrationScope scope;
        const auto first = Definition(kFirst, callbacks.at(index));
        Fixture::gFirst = &first;
        ModuleOperationReport report;
        auto host = StaticModuleHost::Create({std::span(kEntries).first<1>()}, scope, {}, report);
        if (!host)
        {
            return false;
        }
        const auto result = host->Register(kFirst, report);
        const auto expected = index == 0 ? ModuleError::kInvalidDeclaration : ModuleError::kLimitExceeded;
        if (result || result.error() != MakeModuleError(expected) || !scope.Declarations().empty())
        {
            return false;
        }
    }
    return true;
}

bool ConcreteScopesOwnIndependentCopies()
{
    RegistrationScope firstScope;
    RegistrationScope secondScope;
    ModuleOperationReport report;
    {
        std::string name(256, 'a');
        Fixture::gName = name;
        const auto first = Definition(kFirst, Fixture::DeclareFirst);
        Fixture::gFirst = &first;
        auto firstHost = StaticModuleHost::Create({std::span(kEntries).first<1>()}, firstScope, {}, report);
        auto secondHost = StaticModuleHost::Create({std::span(kEntries).first<1>()}, secondScope, {}, report);
        if (!firstHost || !secondHost)
        {
            return false;
        }
        Fixture::gScope = &firstScope;
        if (!firstHost->Register(kFirst, report))
        {
            return false;
        }
        name.assign("second scope");
        Fixture::gName = name;
        Fixture::gScope = &secondScope;
        if (!secondHost->Register(kFirst, report))
        {
            return false;
        }
    }
    return firstScope.Declarations().size() == 1 &&
           firstScope.Declarations().front().DisplayName == std::string(256, 'a') &&
           secondScope.Declarations().size() == 1 && secondScope.Declarations().front().DisplayName == "second scope";
}

bool RejectsCrossHostScopeReentry()
{
    for (const bool fail : {false, true})
    {
        RegistrationScope scope;
        const auto first = Definition(kFirst, Fixture::Reenter);
        const auto second = Definition(kSecond, DeclareSecond);
        Fixture::gFirst = &first;
        Fixture::gSecond = &second;
        Fixture::gObservedBusy = false;
        Fixture::gFailAfterNested = fail;
        ModuleOperationReport report;
        Fixture::gReport = &report;
        auto outer = StaticModuleHost::Create({std::span(kEntries).first<1>()}, scope, {}, report);
        auto inner = StaticModuleHost::Create({std::span(kEntries).subspan<1>()}, scope, {}, report);
        if (!outer || !inner)
        {
            return false;
        }
        Fixture::gNestedHost = &*inner;
        const auto result = outer->Register(kFirst, report);
        if (result.has_value() == fail || !Fixture::gObservedBusy || report.Module != kFirst ||
            report.RelatedModule != ModuleId() ||
            report.PrimaryError != (fail ? std::optional(kOriginal) : std::nullopt) ||
            report.Progress != (fail ? ModuleOperationProgress::kRolledBack : ModuleOperationProgress::kRegistered) ||
            scope.Declarations().size() != (fail ? 0U : 1U) ||
            inner->Modules().front().State != StaticModuleState::kLoaded || !inner->Register(kSecond, report))
        {
            return false;
        }
    }
    Fixture::gFailAfterNested = false;
    return true;
}

bool CrossScopeNestedReportBelongsToOuterOperation()
{
    for (const bool ready : {false, true})
    {
        RegistrationScope outerScope;
        RegistrationScope innerScope;
        const std::array dependencies{ModuleDependency{.Id = kFirst}};
        const auto innerFirst = Definition(kFirst, Fixture::DeclareFirst);
        const std::span<const ModuleDependency> selectedDependencies =
            ready ? std::span<const ModuleDependency>() : dependencies;
        const auto innerSecond = Definition(kSecond, DeclareSecond, selectedDependencies);
        Fixture::gFirst = &innerFirst;
        Fixture::gSecond = &innerSecond;
        ModuleOperationReport report;
        auto inner = StaticModuleHost::Create({kEntries}, innerScope, {}, report);
        const auto outerFirst = Definition(kFirst, Fixture::Reenter);
        Fixture::gFirst = &outerFirst;
        auto outer = StaticModuleHost::Create({std::span(kEntries).first<1>()}, outerScope, {}, report);
        if (!inner || !outer)
        {
            return false;
        }
        Fixture::gNestedHost = &*inner;
        Fixture::gReport = &report;
        const auto result = outer->Register(kFirst, report);
        if (!result || report.PrimaryError || report.Module != kFirst || report.RelatedModule != ModuleId() ||
            report.Progress != ModuleOperationProgress::kRegistered || outerScope.Declarations().size() != 1 ||
            innerScope.Declarations().size() != (ready ? 1U : 0U))
        {
            return false;
        }
    }
    return true;
}
} // namespace

int main()
{
    const std::array tests{
        std::pair{"cross Scope nested report belongs to outer operation",
                  CrossScopeNestedReportBelongsToOuterOperation},
        std::pair{"registers once in dependency order and publishes atomically",
                  RegistersOnceInDependencyOrderAndPublishesAtomically},
        std::pair{"rolls back and preserves original error and progress",
                  RollsBackAndPreservesOriginalErrorAndProgress},
        std::pair{"ignored declaration failure cannot commit", IgnoredDeclarationFailureCannotCommit},
        std::pair{"rejects invalid and oversized declarations", RejectsInvalidAndOversizedDeclarations},
        std::pair{"concrete scopes own independent copies", ConcreteScopesOwnIndependentCopies},
        std::pair{"rejects cross host scope reentry", RejectsCrossHostScopeReentry},
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
