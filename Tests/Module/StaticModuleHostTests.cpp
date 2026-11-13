#include <gtest/gtest.h>

#include "Fixtures/ManagerFixture.h"
#include "ModuleIdentity.h"
#include "ModuleManager.h"
#include "ModuleOperation.h"
#include "Result.h"
#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <RegistrationContext.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace
{
using namespace pg::module;

constexpr ModuleId kFirst{.Bytes = {std::byte{1}}};
constexpr ModuleId kSecond{.Bytes = {std::byte{2}}};
constexpr ModuleId kAbsent{.Bytes = {std::byte{3}}};
constexpr ModuleVersion kVersion{.Major = 1, .Minor = 2, .Patch = 3};

pg::Status Noop(RegistrationContext& /*context*/) noexcept { return {}; }

ModuleDefinition Definition(ModuleId id, std::span<const ModuleDependency> dependencies = {})
{
    return {
        .Descriptor =
            {
                .Id = id,
                .Version = kVersion,
                .Dependencies = dependencies,
                .DisplayName = "module",
                .BuildIdentity = {},
            },
        .RegisterModule = Noop,
    };
}

// Query 没有用户数据参数；串行夹具只在局部定义存活期间提供候选入口。
struct Candidates
{
    Candidates() { Reset(); }
    ~Candidates() { Reset(); }
    Candidates(const Candidates&) = delete;
    Candidates& operator=(const Candidates&) = delete;
    Candidates(Candidates&&) = delete;
    Candidates& operator=(Candidates&&) = delete;

    inline static const ModuleDefinition* gFirst{};
    inline static const ModuleDefinition* gSecond{};
    inline static int gQueries{};
    inline static ModuleOperationReport* gNestedReport{};
    inline static RegistrationScope* gNestedScope{};
    static void Reset() noexcept
    {
        gFirst = nullptr;
        gSecond = nullptr;
        gQueries = 0;
        gNestedReport = nullptr;
        gNestedScope = nullptr;
    }
    static const ModuleDefinition* QueryFirst() noexcept
    {
        ++gQueries;
        if (gNestedReport != nullptr)
        {
            const std::array invalid{StaticCatalogEntry{}};
            [[maybe_unused]] const auto nested =
                pg::module::test::CreateManager({invalid}, *gNestedScope, {}, *gNestedReport);
        }
        return gFirst;
    }
    static const ModuleDefinition* QuerySecond() noexcept
    {
        ++gQueries;
        return gSecond;
    }
};

constexpr std::array kEntries{StaticCatalogEntry{Candidates::QueryFirst}, StaticCatalogEntry{Candidates::QuerySecond}};

bool IsError(const auto& result, ModuleError code) { return !result && result.error() == MakeModuleError(code); }

TEST(ModuleManager, CopiesMetadataBeforeSourceDestruction)
{
    const Candidates callbackState;
    RegistrationScope scope;
    ModuleOperationReport report;
    auto host = [&]
    {
        std::string name = "owned name";
        std::string build = "owned build";
        std::array dependencies{ModuleDependency{.Id = kSecond, .Version = kVersion}};
        auto first = Definition(kFirst, dependencies);
        first.Descriptor.DisplayName = name;
        first.Descriptor.BuildIdentity = build;
        auto const second = Definition(kSecond);
        Candidates::gFirst = &first;
        Candidates::gSecond = &second;
        Candidates::gQueries = 0;
        auto result = pg::module::test::CreateManager({kEntries}, scope, {}, report);
        name.assign("changed");
        build.assign("changed");
        dependencies.front().Id = kAbsent;
        return result;
    }();
    Candidates::gFirst = nullptr;
    Candidates::gSecond = nullptr;
    ASSERT_TRUE(host);
    ASSERT_EQ(Candidates::gQueries, 2);
    ASSERT_EQ(pg::module::test::BusinessSnapshots(*host).size(), 2);
    ASSERT_EQ(report.Progress, ModuleOperationProgress::kLoaded);
    ASSERT_FALSE(report.PrimaryError);
    auto const moved = std::move(*host);
    const auto descriptor = pg::module::test::BusinessSnapshots(moved).front().Descriptor;
    const auto order = pg::module::test::BusinessOrder(moved);
    ASSERT_EQ(descriptor.DisplayName, "owned name");
    ASSERT_EQ(descriptor.BuildIdentity, "owned build");
    ASSERT_EQ(descriptor.Dependencies.front().Id, kSecond);
    ASSERT_EQ(order.size(), 2);
    ASSERT_EQ(order.front(), kSecond);
    ASSERT_EQ(order.back(), kFirst);
}

TEST(ModuleManager, RejectsInvalidDefinitionsAndIdentity)
{
    const Candidates callbackState;
    RegistrationScope scope;
    ModuleOperationReport report;
    const std::array emptyQuery{StaticCatalogEntry{}};
    ASSERT_TRUE(
        IsError(pg::module::test::CreateManager({emptyQuery}, scope, {}, report), ModuleError::kInvalidDefinition));
    const auto one = std::span(kEntries).first<1>();
    Candidates::gFirst = nullptr;
    ASSERT_TRUE(IsError(pg::module::test::CreateManager({one}, scope, {}, report), ModuleError::kInvalidDefinition));
    auto first = Definition(kFirst);
    Candidates::gFirst = &first;
    first.RegisterModule = nullptr;
    ASSERT_TRUE(IsError(pg::module::test::CreateManager({one}, scope, {}, report), ModuleError::kInvalidDefinition));
    first = Definition({});
    ASSERT_TRUE(IsError(pg::module::test::CreateManager({one}, scope, {}, report), ModuleError::kInvalidDescriptor));
    first = Definition(kFirst);
    first.Descriptor.ProtocolVersion = 2;
    ASSERT_TRUE(IsError(pg::module::test::CreateManager({one}, scope, {}, report), ModuleError::kInvalidDescriptor));
    first = Definition(kFirst);
    first.Descriptor.Version = {};
    const auto zeroVersion = pg::module::test::CreateManager({one}, scope, {}, report);
    ASSERT_TRUE(zeroVersion);
    first = Definition(kFirst);
    Candidates::gSecond = &first;
    ASSERT_TRUE(IsError(pg::module::test::CreateManager({kEntries}, scope, {}, report), ModuleError::kDuplicateModule));
    ASSERT_EQ(report.Module, kFirst);
    ASSERT_EQ(report.PrimaryError, MakeModuleError(ModuleError::kDuplicateModule));
}

TEST(ModuleManager, EnforcesLimitsBeforeCopying)
{
    const Candidates callbackState;
    RegistrationScope scope;
    ModuleOperationReport report;
    auto first = Definition(kFirst);
    Candidates::gFirst = &first;
    const auto one = std::span(kEntries).first<1>();
    std::string name(256, 'n');
    first.Descriptor.DisplayName = name;
    ASSERT_TRUE(pg::module::test::CreateManager({one}, scope, {}, report));
    name.push_back('n');
    first.Descriptor.DisplayName = name;
    ASSERT_TRUE(IsError(pg::module::test::CreateManager({one}, scope, {}, report), ModuleError::kLimitExceeded));
    first = Definition(kFirst);
    first.Descriptor.BuildIdentity = name;
    ASSERT_TRUE(IsError(pg::module::test::CreateManager({one}, scope, {}, report), ModuleError::kLimitExceeded));
    std::array<ModuleDependency, 65> dependencies{};
    first = Definition(kFirst, dependencies);
    ASSERT_TRUE(IsError(pg::module::test::CreateManager({one}, scope, {}, report), ModuleError::kLimitExceeded));
    const std::array<StaticCatalogEntry, 257> entries{};
    ASSERT_TRUE(IsError(pg::module::test::CreateManager({entries}, scope, {}, report), ModuleError::kLimitExceeded));
}

TEST(ModuleManager, ValidatesDependencies)
{
    const Candidates callbackState;
    RegistrationScope scope;
    ModuleOperationReport report;
    auto second = Definition(kSecond);
    Candidates::gSecond = &second;
    std::array dependencies{ModuleDependency{.Id = kSecond, .Version = kVersion}};
    auto first = Definition(kFirst, dependencies);
    Candidates::gFirst = &first;
    const auto one = std::span(kEntries).first<1>();
    ASSERT_TRUE(IsError(pg::module::test::CreateManager({one}, scope, {}, report), ModuleError::kMissingDependency));
    ASSERT_EQ(report.Module, kFirst);
    ASSERT_EQ(report.RelatedModule, kSecond);
    constexpr std::array kWrongVersions{
        ModuleVersion{.Major = 0, .Minor = 2, .Patch = 3},
        ModuleVersion{.Major = 1, .Minor = 0, .Patch = 3},
        ModuleVersion{.Major = 1, .Minor = 2, .Patch = 0},
    };
    for (const ModuleVersion version : kWrongVersions)
    {
        second.Descriptor.Version = version;
        ASSERT_TRUE(
            IsError(pg::module::test::CreateManager({kEntries}, scope, {}, report), ModuleError::kVersionMismatch));
    }
    second.Descriptor.Version = kVersion;
    for (const auto id : {ModuleId(), kFirst})
    {
        dependencies.front().Id = id;
        ASSERT_TRUE(
            IsError(pg::module::test::CreateManager({kEntries}, scope, {}, report), ModuleError::kInvalidDependency));
    }
    dependencies.front().Id = kSecond;
    dependencies.front().Kind = std::bit_cast<DependencyKind>(std::uint8_t{255});
    ASSERT_TRUE(
        IsError(pg::module::test::CreateManager({kEntries}, scope, {}, report), ModuleError::kInvalidDependency));
    dependencies.front().Kind = DependencyKind::kRequired;
    const std::array duplicate{dependencies.front(), dependencies.front()};
    first.Descriptor.Dependencies = duplicate;
    ASSERT_TRUE(
        IsError(pg::module::test::CreateManager({kEntries}, scope, {}, report), ModuleError::kInvalidDependency));
}

TEST(ModuleManager, OptionalSelectionIsExplicitAndRejectsCycles)
{
    const Candidates callbackState;
    RegistrationScope scope;
    ModuleOperationReport report;
    constexpr ModuleDependency kOptionalDependency{
        .Id = kSecond,
        .Version = kVersion,
        .Kind = DependencyKind::kOptional,
    };
    const std::array dependencies{kOptionalDependency};
    auto const first = Definition(kFirst, dependencies);
    auto second = Definition(kSecond);
    Candidates::gFirst = &first;
    Candidates::gSecond = &second;
    const std::array selected{OptionalDependencyBinding{.Dependent = kFirst, .Dependency = kSecond}};
    const auto reason = [&](DependencyBindingReason expected, bool present)
    {
        const std::span<const StaticCatalogEntry> entries = kEntries;
        auto host = pg::module::test::CreateManager({present ? entries : entries.first(1)}, scope, {}, report);
        return host && pg::module::test::BusinessSnapshots(*host).front().Bindings.front().Reason == expected &&
               !pg::module::test::BusinessSnapshots(*host).front().Bindings.front().IsBound();
    };
    ASSERT_TRUE(reason(DependencyBindingReason::kNotSelected, true));
    ASSERT_TRUE(reason(DependencyBindingReason::kMissing, false));
    second.Descriptor.Version = {};
    ASSERT_TRUE(reason(DependencyBindingReason::kVersionMismatch, true));
    ASSERT_TRUE(
        IsError(pg::module::test::CreateManager({kEntries}, scope, selected, report), ModuleError::kVersionMismatch));
    ASSERT_TRUE(IsError(pg::module::test::CreateManager({std::span(kEntries).first<1>()}, scope, selected, report),
                        ModuleError::kMissingDependency));
    second.Descriptor.Version = kVersion;
    auto bound = pg::module::test::CreateManager({kEntries}, scope, selected, report);
    ASSERT_TRUE(bound);
    ASSERT_TRUE(pg::module::test::BusinessSnapshots(*bound).front().Bindings.front().IsBound());
    ASSERT_EQ(pg::module::test::BusinessOrder(*bound).front(), kSecond);
    for (const auto binding : {
             OptionalDependencyBinding{.Dependent = kAbsent, .Dependency = kSecond},
             OptionalDependencyBinding{.Dependent = kFirst, .Dependency = kAbsent},
             OptionalDependencyBinding{.Dependent = kSecond, .Dependency = kFirst},
         })
    {
        const std::array invalid{binding};
        ASSERT_TRUE(IsError(pg::module::test::CreateManager({kEntries}, scope, invalid, report),
                            ModuleError::kInvalidOptionalBinding));
    }
    const std::array duplicate{selected.front(), selected.front()};
    ASSERT_TRUE(IsError(pg::module::test::CreateManager({kEntries}, scope, duplicate, report),
                        ModuleError::kInvalidOptionalBinding));
    const std::array backEdge{ModuleDependency{.Id = kFirst, .Version = kVersion}};
    second.Descriptor.Dependencies = backEdge;
    ASSERT_TRUE(pg::module::test::CreateManager({kEntries}, scope, {}, report));
    ASSERT_TRUE(
        IsError(pg::module::test::CreateManager({kEntries}, scope, selected, report), ModuleError::kDependencyCycle));
}
TEST(ModuleManager, NestedQueryCannotPolluteOuterReport)
{
    const Candidates callbackState;
    RegistrationScope scope;
    ModuleOperationReport report;
    const auto first = Definition(kFirst);
    Candidates::gFirst = &first;
    Candidates::gNestedReport = &report;
    Candidates::gNestedScope = &scope;
    const auto host = pg::module::test::CreateManager({std::span(kEntries).first<1>()}, scope, {}, report);
    Candidates::gNestedReport = nullptr;
    Candidates::gNestedScope = nullptr;
    ASSERT_TRUE(host);
    ASSERT_FALSE(report.PrimaryError);
    ASSERT_EQ(report.Progress, ModuleOperationProgress::kLoaded);
    ASSERT_EQ(report.Module, ModuleId());
    ASSERT_EQ(report.RelatedModule, ModuleId());
}
TEST(ModuleManager, IncrementalBatchDoesNotRebindExistingOptionalDependencies)
{
    const Candidates callbackState;
    RegistrationScope scope;
    ModuleOperationReport report;
    const std::array dependencies{
        ModuleDependency{.Id = kSecond, .Version = kVersion, .Kind = DependencyKind::kOptional}};
    const auto first = Definition(kFirst, dependencies);
    const auto second = Definition(kSecond);
    Candidates::gFirst = &first;
    Candidates::gSecond = &second;
    auto manager = pg::module::test::CreateManager({std::span(kEntries).first(1)}, scope, {}, report);
    ASSERT_TRUE(manager);
    const auto original = *manager->Find(kFirst);
    ASSERT_EQ(manager->Inspect(original)->Bindings.front().Reason, DependencyBindingReason::kMissing);
    const std::array added{ModuleLoadCandidate{StaticModuleCandidate{Candidates::QuerySecond}}};
    ASSERT_TRUE(manager->Load(added, {}, report));
    EXPECT_EQ(manager->Inspect(original)->Bindings.front().Reason, DependencyBindingReason::kMissing);
    const auto duplicate = manager->Load(added, {}, report);
    EXPECT_EQ(duplicate.error(), MakeModuleError(ModuleError::kDuplicateModule));
    EXPECT_EQ(manager->Modules().size(), 5U);
    EXPECT_TRUE(manager->Inspect(original));
}

} // namespace
