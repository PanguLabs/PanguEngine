#include <gtest/gtest.h>

#include "Fixtures/ManagerFixture.h"
#include "ModuleDeclaration.h"
#include <Error.h>
#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <ModuleIdentity.h>
#include <RegistrationContext.h>
#include <Result.h>

#include <array>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace
{
using namespace pg::module;
constexpr ModuleId kFirst{.Bytes = {std::byte{1}}};
constexpr ModuleId kSecond{.Bytes = {std::byte{2}}};
constexpr DeclarationId kDeclarationFirst{.Bytes = {std::byte{1}}};
constexpr DeclarationId kDeclarationSecond{.Bytes = {std::byte{2}}};
constexpr pg::Error kOriginal{pg::ErrorDomainId::Constant<0x746573742e726567ULL>(), 91,
                              pg::NativeErrorCode{.Source = pg::NativeErrorSourceId{17}, .Value = 123}};

// 串行回调/Query 夹具，只在测试期间借用局部数据。
struct Fixture
{
    Fixture() { Reset(); }
    ~Fixture() { Reset(); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    Fixture(Fixture&&) = delete;
    Fixture& operator=(Fixture&&) = delete;

    inline static const ModuleDefinition* gFirst{};
    inline static const ModuleDefinition* gSecond{};
    inline static const RegistrationScope* gScope{};
    inline static ModuleManager* gNestedHost{};
    inline static std::string_view gName;
    inline static bool gObservedAtomic{};
    inline static bool gObservedBusy{};
    inline static bool gFailAfterNested{};
    inline static ModuleOperationReport* gReport{};
    inline static int gCalls{};
    static void Reset() noexcept
    {
        gFirst = nullptr;
        gSecond = nullptr;
        gScope = nullptr;
        gNestedHost = nullptr;
        gName = {};
        gObservedAtomic = false;
        gObservedBusy = false;
        gFailAfterNested = false;
        gReport = nullptr;
        gCalls = 0;
    }
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
        const auto nested = gNestedHost->Register(gNestedHost->Find(kSecond).value_or(ModuleHandle{}), *gReport);
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

TEST(Registration, RegistersOnceInDependencyOrderAndPublishesAtomically)
{
    const Fixture callbackState;
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
    auto host = pg::module::test::CreateManager({kEntries}, scope, {}, report);
    ASSERT_TRUE(host);
    const auto early = host->Register(host->Find(kFirst).value_or(ModuleHandle{}), report);
    ASSERT_FALSE(early);
    ASSERT_EQ(early.error(), MakeModuleError(ModuleError::kDependencyNotRegistered));
    ASSERT_EQ(Fixture::gCalls, 0);
    ASSERT_EQ(report.Progress, ModuleOperationProgress::kUnchanged);
    ASSERT_EQ(report.RelatedModule, kSecond);
    ASSERT_EQ(pg::module::test::BusinessSnapshots(*host).front().State, ModuleState::kLoaded);
    for (const auto id : pg::module::test::BusinessOrder(*host))
    {
        ASSERT_TRUE(host->Register(host->Find(id).value_or(ModuleHandle{}), report));
        ASSERT_FALSE(report.PrimaryError);
        ASSERT_EQ(report.Progress, ModuleOperationProgress::kRegistered);
    }
    const auto repeated = host->Register(host->Find(kFirst).value_or(ModuleHandle{}), report);
    ASSERT_FALSE(repeated);
    ASSERT_EQ(repeated.error(), MakeModuleError(ModuleError::kInvalidState));
    ASSERT_EQ(Fixture::gCalls, 1);
    ASSERT_TRUE(Fixture::gObservedAtomic);
    ASSERT_EQ(scope.Declarations().size(), 2);
    ASSERT_EQ(scope.Declarations().front().Owner, kSecond);
    ASSERT_EQ(scope.Declarations().back().Owner, kFirst);
    ASSERT_EQ(scope.Declarations().back().DisplayName, "first");
    ASSERT_EQ(report.Progress, ModuleOperationProgress::kUnchanged);
}

TEST(Registration, RollsBackAndPreservesOriginalErrorAndProgress)
{
    const Fixture callbackState;
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
    auto host = pg::module::test::CreateManager({kEntries}, scope, {}, report);
    ASSERT_TRUE(host);
    ASSERT_TRUE(host->Register(host->Find(kSecond).value_or(ModuleHandle{}), report));
    const auto failed = host->Register(host->Find(kFirst).value_or(ModuleHandle{}), report);
    const auto snapshot = report;
    const auto retry = host->Register(host->Find(kFirst).value_or(ModuleHandle{}), report);
    ASSERT_FALSE(failed);
    ASSERT_EQ(failed.error(), kOriginal);
    ASSERT_EQ(snapshot.PrimaryError, kOriginal);
    ASSERT_EQ(snapshot.Progress, ModuleOperationProgress::kCleanupPending);
    ASSERT_EQ(pg::module::test::BusinessSnapshots(*host).front().State, ModuleState::kCleanupPending);
    ASSERT_FALSE(retry);
    ASSERT_EQ(report.Progress, ModuleOperationProgress::kUnchanged);
    ASSERT_EQ(scope.Declarations().size(), 1);
    ASSERT_EQ(scope.Declarations().front().Owner, kSecond);
    // 相同 Error 值仍须区分是否进入回调。
    RegistrationScope otherScope;
    const auto identicalError = [](RegistrationContext&) noexcept -> pg::Status
    { return std::unexpected(MakeModuleError(ModuleError::kInvalidHandle)); };
    const auto other = Definition(kFirst, identicalError);
    Fixture::gFirst = &other;
    auto otherHost = pg::module::test::CreateManager({std::span(kEntries).first<1>()}, otherScope, {}, report);
    ASSERT_TRUE(otherHost);
    const auto absent = otherHost->Register(otherHost->Find(kSecond).value_or(ModuleHandle{}), report);
    const auto before = report;
    const auto callback = otherHost->Register(otherHost->Find(kFirst).value_or(ModuleHandle{}), report);
    ASSERT_FALSE(absent);
    ASSERT_FALSE(callback);
    ASSERT_EQ(absent.error(), callback.error());
    ASSERT_EQ(before.Progress, ModuleOperationProgress::kUnchanged);
    ASSERT_EQ(report.Progress, ModuleOperationProgress::kCleanupPending);
}

TEST(Registration, IgnoredDeclarationFailureCannotCommit)
{
    const Fixture callbackState;
    for (const bool existing : {false, true})
    {
        SCOPED_TRACE(existing);
        RegistrationScope scope;
        const auto duplicate = [](RegistrationContext& context) noexcept -> pg::Status
        {
            const auto first = context.Declare(kDeclarationFirst, "pending");
            if (!first)
            {
                return first;
            }
            // 故意忽略声明错误，验证事务仍锁存原错。
            [[maybe_unused]] const auto ignoredConflict = context.Declare(kDeclarationSecond, "conflict");
            [[maybe_unused]] const auto ignoredDuplicate = context.Declare(kDeclarationSecond, "duplicate pending");
            return {};
        };
        const auto first = Definition(kFirst, duplicate);
        const auto second = Definition(kSecond, DeclareSecond);
        Fixture::gFirst = &first;
        Fixture::gSecond = &second;
        ModuleOperationReport report;
        auto host = pg::module::test::CreateManager({kEntries}, scope, {}, report);
        ASSERT_TRUE(host);
        if (existing)
        {
            ASSERT_TRUE(host->Register(host->Find(kSecond).value_or(ModuleHandle{}), report));
        }
        const auto result = host->Register(host->Find(kFirst).value_or(ModuleHandle{}), report);
        ASSERT_FALSE(result);
        ASSERT_EQ(result.error(), MakeModuleError(ModuleError::kDeclarationConflict));
        ASSERT_EQ(scope.Declarations().size(), (existing ? 1U : 0U));
        ASSERT_EQ(report.Progress, ModuleOperationProgress::kCleanupPending);
    }
}

TEST(Registration, RejectsInvalidAndOversizedDeclarations)
{
    const Fixture callbackState;
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
        SCOPED_TRACE(index);
        RegistrationScope scope;
        const auto first = Definition(kFirst, callbacks.at(index));
        Fixture::gFirst = &first;
        ModuleOperationReport report;
        auto host = pg::module::test::CreateManager({std::span(kEntries).first<1>()}, scope, {}, report);
        ASSERT_TRUE(host);
        const auto result = host->Register(host->Find(kFirst).value_or(ModuleHandle{}), report);
        const auto expected = index == 0 ? ModuleError::kInvalidDeclaration : ModuleError::kLimitExceeded;
        ASSERT_FALSE(result);
        ASSERT_EQ(result.error(), MakeModuleError(expected));
        ASSERT_TRUE(scope.Declarations().empty());
    }
}

TEST(Registration, ConcreteScopesOwnIndependentCopies)
{
    const Fixture callbackState;
    RegistrationScope firstScope;
    RegistrationScope secondScope;
    ModuleOperationReport report;
    {
        std::string name(256, 'a');
        Fixture::gName = name;
        const auto first = Definition(kFirst, Fixture::DeclareFirst);
        Fixture::gFirst = &first;
        auto firstHost = pg::module::test::CreateManager({std::span(kEntries).first<1>()}, firstScope, {}, report);
        auto secondHost = pg::module::test::CreateManager({std::span(kEntries).first<1>()}, secondScope, {}, report);
        ASSERT_TRUE(firstHost);
        ASSERT_TRUE(secondHost);
        Fixture::gScope = &firstScope;
        ASSERT_TRUE(firstHost->Register(firstHost->Find(kFirst).value_or(ModuleHandle{}), report));
        name.assign("second scope");
        Fixture::gName = name;
        Fixture::gScope = &secondScope;
        ASSERT_TRUE(secondHost->Register(secondHost->Find(kFirst).value_or(ModuleHandle{}), report));
        ASSERT_EQ(firstScope.Declarations().size(), 1);
        ASSERT_EQ(firstScope.Declarations().front().DisplayName, std::string(256, 'a'));
        ASSERT_EQ(secondScope.Declarations().size(), 1);
        ASSERT_EQ(secondScope.Declarations().front().DisplayName, "second scope");
        // 深复制检查完成后，Shutdown 撤销本 Host 的声明。
    }
    ASSERT_TRUE(firstScope.Declarations().empty());
    ASSERT_TRUE(secondScope.Declarations().empty());
}

TEST(Registration, RejectsCrossHostScopeReentry)
{
    const Fixture callbackState;
    for (const bool fail : {false, true})
    {
        SCOPED_TRACE(fail);
        RegistrationScope scope;
        const auto first = Definition(kFirst, Fixture::Reenter);
        const auto second = Definition(kSecond, DeclareSecond);
        Fixture::gFirst = &first;
        Fixture::gSecond = &second;
        Fixture::gObservedBusy = false;
        Fixture::gFailAfterNested = fail;
        ModuleOperationReport report;
        Fixture::gReport = &report;
        auto outer = pg::module::test::CreateManager({std::span(kEntries).first<1>()}, scope, {}, report);
        auto inner = pg::module::test::CreateManager({std::span(kEntries).subspan<1>()}, scope, {}, report);
        ASSERT_TRUE(outer);
        ASSERT_TRUE(inner);
        Fixture::gNestedHost = &*inner;
        const auto result = outer->Register(outer->Find(kFirst).value_or(ModuleHandle{}), report);
        ASSERT_NE(result.has_value(), fail);
        ASSERT_TRUE(Fixture::gObservedBusy);
        ASSERT_EQ(report.Module, kFirst);
        ASSERT_EQ(report.RelatedModule, ModuleId());
        ASSERT_EQ(report.PrimaryError, (fail ? std::optional(kOriginal) : std::nullopt));
        ASSERT_EQ(report.Progress,
                  (fail ? ModuleOperationProgress::kCleanupPending : ModuleOperationProgress::kRegistered));
        ASSERT_EQ(scope.Declarations().size(), (fail ? 0U : 1U));
        ASSERT_EQ(pg::module::test::BusinessSnapshots(*inner).front().State, ModuleState::kLoaded);
        ASSERT_TRUE(inner->Register(inner->Find(kSecond).value_or(ModuleHandle{}), report));
    }
    Fixture::gFailAfterNested = false;
}

TEST(Registration, CrossScopeNestedReportBelongsToOuterOperation)
{
    const Fixture callbackState;
    Fixture::gFailAfterNested = false;
    for (const bool ready : {false, true})
    {
        SCOPED_TRACE(ready);
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
        auto inner = pg::module::test::CreateManager({kEntries}, innerScope, {}, report);
        const auto outerFirst = Definition(kFirst, Fixture::Reenter);
        Fixture::gFirst = &outerFirst;
        auto outer = pg::module::test::CreateManager({std::span(kEntries).first<1>()}, outerScope, {}, report);
        ASSERT_TRUE(inner);
        ASSERT_TRUE(outer);
        Fixture::gNestedHost = &*inner;
        Fixture::gReport = &report;
        const auto result = outer->Register(outer->Find(kFirst).value_or(ModuleHandle{}), report);
        ASSERT_TRUE(result);
        ASSERT_FALSE(report.PrimaryError);
        ASSERT_EQ(report.Module, kFirst);
        ASSERT_EQ(report.RelatedModule, ModuleId());
        ASSERT_EQ(report.Progress, ModuleOperationProgress::kRegistered);
        ASSERT_EQ(outerScope.Declarations().size(), 1);
        ASSERT_EQ(innerScope.Declarations().size(), (ready ? 1U : 0U));
    }
}
} // namespace
