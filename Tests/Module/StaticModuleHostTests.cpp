#include "ModuleIdentity.h"
#include "Result.h"
#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <RegistrationContext.h>
#include <StaticModuleHost.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
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

// Query has no user-data argument. This serial fixture supplies bounded-lifetime candidates.
struct Candidates
{
    inline static const ModuleDefinition* gFirst{};
    inline static const ModuleDefinition* gSecond{};
    inline static int gQueries{};
    inline static ModuleOperationReport* gNestedReport{};
    inline static RegistrationScope* gNestedScope{};
    static const ModuleDefinition* QueryFirst() noexcept
    {
        ++gQueries;
        if (gNestedReport != nullptr)
        {
            const std::array invalid{StaticCatalogEntry{}};
            [[maybe_unused]] const auto nested = StaticModuleHost::Create({invalid}, *gNestedScope, {}, *gNestedReport);
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

bool CopiesMetadataBeforeSourceDestruction()
{
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
        auto result = StaticModuleHost::Create({kEntries}, scope, {}, report);
        name.assign("changed");
        build.assign("changed");
        dependencies.front().Id = kAbsent;
        return result;
    }();
    Candidates::gFirst = nullptr;
    Candidates::gSecond = nullptr;
    if (!host || Candidates::gQueries != 2 || host->Modules().size() != 2 ||
        report.Progress != ModuleOperationProgress::kLoaded || report.PrimaryError)
    {
        return false;
    }
    auto const moved = std::move(*host);
    const auto& descriptor = moved.Modules().front().Descriptor;
    const auto order = moved.RegistrationOrder();
    return descriptor.DisplayName == "owned name" && descriptor.BuildIdentity == "owned build" &&
           descriptor.Dependencies.front().Id == kSecond && order.size() == 2 && order.front() == kSecond &&
           order.back() == kFirst;
}

bool RejectsInvalidDefinitionsAndIdentity()
{
    RegistrationScope scope;
    ModuleOperationReport report;
    const std::array emptyQuery{StaticCatalogEntry{}};
    if (!IsError(StaticModuleHost::Create({emptyQuery}, scope, {}, report), ModuleError::kInvalidDefinition))
    {
        return false;
    }
    const auto one = std::span(kEntries).first<1>();
    Candidates::gFirst = nullptr;
    if (!IsError(StaticModuleHost::Create({one}, scope, {}, report), ModuleError::kInvalidDefinition))
    {
        return false;
    }
    auto first = Definition(kFirst);
    Candidates::gFirst = &first;
    first.RegisterModule = nullptr;
    if (!IsError(StaticModuleHost::Create({one}, scope, {}, report), ModuleError::kInvalidDefinition))
    {
        return false;
    }
    first = Definition({});
    if (!IsError(StaticModuleHost::Create({one}, scope, {}, report), ModuleError::kInvalidDescriptor))
    {
        return false;
    }
    first = Definition(kFirst);
    first.Descriptor.ProtocolVersion = 2;
    if (!IsError(StaticModuleHost::Create({one}, scope, {}, report), ModuleError::kInvalidDescriptor))
    {
        return false;
    }
    first = Definition(kFirst);
    first.Descriptor.Version = {};
    const auto zeroVersion = StaticModuleHost::Create({one}, scope, {}, report);
    if (!zeroVersion)
    {
        return false;
    }
    const StaticModuleCatalog nested{};
    first.LinkedStaticModules = &nested;
    if (!IsError(StaticModuleHost::Create({one}, scope, {}, report), ModuleError::kInvalidDefinition))
    {
        return false;
    }
    first = Definition(kFirst);
    Candidates::gSecond = &first;
    return IsError(StaticModuleHost::Create({kEntries}, scope, {}, report), ModuleError::kDuplicateModule) &&
           report.Module == kFirst && report.PrimaryError == MakeModuleError(ModuleError::kDuplicateModule);
}

bool EnforcesLimitsBeforeCopying()
{
    RegistrationScope scope;
    ModuleOperationReport report;
    auto first = Definition(kFirst);
    Candidates::gFirst = &first;
    const auto one = std::span(kEntries).first<1>();
    std::string name(256, 'n');
    first.Descriptor.DisplayName = name;
    if (!StaticModuleHost::Create({one}, scope, {}, report))
    {
        return false;
    }
    name.push_back('n');
    first.Descriptor.DisplayName = name;
    if (!IsError(StaticModuleHost::Create({one}, scope, {}, report), ModuleError::kLimitExceeded))
    {
        return false;
    }
    first = Definition(kFirst);
    first.Descriptor.BuildIdentity = name;
    if (!IsError(StaticModuleHost::Create({one}, scope, {}, report), ModuleError::kLimitExceeded))
    {
        return false;
    }
    std::array<ModuleDependency, 65> dependencies{};
    first = Definition(kFirst, dependencies);
    if (!IsError(StaticModuleHost::Create({one}, scope, {}, report), ModuleError::kLimitExceeded))
    {
        return false;
    }
    const std::array<StaticCatalogEntry, 257> entries{};
    return IsError(StaticModuleHost::Create({entries}, scope, {}, report), ModuleError::kLimitExceeded);
}

bool ValidatesDependencies()
{
    RegistrationScope scope;
    ModuleOperationReport report;
    auto second = Definition(kSecond);
    Candidates::gSecond = &second;
    std::array dependencies{ModuleDependency{.Id = kSecond, .Version = kVersion}};
    auto first = Definition(kFirst, dependencies);
    Candidates::gFirst = &first;
    const auto one = std::span(kEntries).first<1>();
    if (!IsError(StaticModuleHost::Create({one}, scope, {}, report), ModuleError::kMissingDependency) ||
        report.Module != kFirst || report.RelatedModule != kSecond)
    {
        return false;
    }
    constexpr std::array kWrongVersions{
        ModuleVersion{.Major = 0, .Minor = 2, .Patch = 3},
        ModuleVersion{.Major = 1, .Minor = 0, .Patch = 3},
        ModuleVersion{.Major = 1, .Minor = 2, .Patch = 0},
    };
    for (const ModuleVersion version : kWrongVersions)
    {
        second.Descriptor.Version = version;
        if (!IsError(StaticModuleHost::Create({kEntries}, scope, {}, report), ModuleError::kVersionMismatch))
        {
            return false;
        }
    }
    second.Descriptor.Version = kVersion;
    for (const auto id : {ModuleId(), kFirst})
    {
        dependencies.front().Id = id;
        if (!IsError(StaticModuleHost::Create({kEntries}, scope, {}, report), ModuleError::kInvalidDependency))
        {
            return false;
        }
    }
    dependencies.front().Id = kSecond;
    dependencies.front().Kind = std::bit_cast<DependencyKind>(std::uint8_t{255});
    if (!IsError(StaticModuleHost::Create({kEntries}, scope, {}, report), ModuleError::kInvalidDependency))
    {
        return false;
    }
    dependencies.front().Kind = DependencyKind::kRequired;
    const std::array duplicate{dependencies.front(), dependencies.front()};
    first.Descriptor.Dependencies = duplicate;
    return IsError(StaticModuleHost::Create({kEntries}, scope, {}, report), ModuleError::kInvalidDependency);
}

bool OptionalSelectionIsExplicitAndRejectsCycles()
{
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
        auto host = StaticModuleHost::Create({present ? entries : entries.first(1)}, scope, {}, report);
        return host && host->Modules().front().Bindings.front().Reason == expected &&
               !host->Modules().front().Bindings.front().IsBound();
    };
    if (!reason(DependencyBindingReason::kNotSelected, true) || !reason(DependencyBindingReason::kMissing, false))
    {
        return false;
    }
    second.Descriptor.Version = {};
    if (!reason(DependencyBindingReason::kVersionMismatch, true) ||
        !IsError(StaticModuleHost::Create({kEntries}, scope, selected, report), ModuleError::kVersionMismatch) ||
        !IsError(StaticModuleHost::Create({std::span(kEntries).first<1>()}, scope, selected, report),
                 ModuleError::kMissingDependency))
    {
        return false;
    }
    second.Descriptor.Version = kVersion;
    auto bound = StaticModuleHost::Create({kEntries}, scope, selected, report);
    if (!bound || !bound->Modules().front().Bindings.front().IsBound() || bound->RegistrationOrder().front() != kSecond)
    {
        return false;
    }
    for (const auto binding : {
             OptionalDependencyBinding{.Dependent = kAbsent, .Dependency = kSecond},
             OptionalDependencyBinding{.Dependent = kFirst, .Dependency = kAbsent},
             OptionalDependencyBinding{.Dependent = kSecond, .Dependency = kFirst},
         })
    {
        const std::array invalid{binding};
        if (!IsError(StaticModuleHost::Create({kEntries}, scope, invalid, report),
                     ModuleError::kInvalidOptionalBinding))
        {
            return false;
        }
    }
    const std::array duplicate{selected.front(), selected.front()};
    if (!IsError(StaticModuleHost::Create({kEntries}, scope, duplicate, report), ModuleError::kInvalidOptionalBinding))
    {
        return false;
    }
    const std::array backEdge{ModuleDependency{.Id = kFirst, .Version = kVersion}};
    second.Descriptor.Dependencies = backEdge;
    if (!StaticModuleHost::Create({kEntries}, scope, {}, report))
    {
        return false;
    }
    return IsError(StaticModuleHost::Create({kEntries}, scope, selected, report), ModuleError::kDependencyCycle);
}
bool NestedQueryCannotPolluteOuterReport()
{
    RegistrationScope scope;
    ModuleOperationReport report;
    const auto first = Definition(kFirst);
    Candidates::gFirst = &first;
    Candidates::gNestedReport = &report;
    Candidates::gNestedScope = &scope;
    const auto host = StaticModuleHost::Create({std::span(kEntries).first<1>()}, scope, {}, report);
    Candidates::gNestedReport = nullptr;
    Candidates::gNestedScope = nullptr;
    return host && !report.PrimaryError && report.Progress == ModuleOperationProgress::kLoaded &&
           report.Module == ModuleId() && report.RelatedModule == ModuleId();
}
} // namespace

int main()
{
    const std::array tests{
        std::pair{"nested Query cannot pollute outer report", NestedQueryCannotPolluteOuterReport},
        std::pair{"copies metadata before source destruction", CopiesMetadataBeforeSourceDestruction},
        std::pair{"rejects invalid definitions and identity", RejectsInvalidDefinitionsAndIdentity},
        std::pair{"enforces limits before copying", EnforcesLimitsBeforeCopying},
        std::pair{"validates dependencies", ValidatesDependencies},
        std::pair{"optional selection is explicit and rejects cycles", OptionalSelectionIsExplicitAndRejectsCycles},
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
