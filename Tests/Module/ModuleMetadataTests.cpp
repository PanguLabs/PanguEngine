#include "Error.h"
#include "MetaIdentity.h"
#include "MetaInfo.h"
#include "ModuleDescriptor.h"
#include "ModuleOperation.h"
#include "Result.h"
#include <FoundationModule.h>
#include <LogModule.h>
#include <ManualModule.h>
#include <ModuleBuiltin.h>
#include <ModuleError.h>
#include <ModuleManager.h>
#include <ModuleMetaProvider.h>
#include <ModuleMetadataAccess.h>
#include <RegistrationContext.h>
#include <array>
#include <cstddef>
#include <gtest/gtest.h>
#include <optional>
#include <typeinfo>
#include <utility>

#ifdef PG_TEST_MODULAR
#include <ManualModulePath.h>
#endif

namespace pg::module
{
namespace
{
// 致命断言只退出辅助函数，调用者仍执行借用释放与关闭。
template <class T>
void ExpectError(const Result<T>& result, Error expected)
{
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), expected);
}

struct AdmissionFixture
{
    inline static ModuleDefinition gDefinition;
    inline static const ModuleManager* gManager{};
    inline static std::optional<Error> gReentryError;
    static const ModuleDefinition* Query() noexcept { return &gDefinition; }
    static Status Register(RegistrationContext& /*unused*/) noexcept
    {
        const auto result = detail::ModuleMetadataAccess::Acquire(*gManager, test::kWrongManualOwner, typeid(int));
        if (!result)
        {
            gReentryError = result.error();
        }
        return {};
    }
};
void ExpectRejected(const ModuleMetaProvider& provider)
{
    AdmissionFixture::gDefinition = *test::QueryManualModule();
    AdmissionFixture::gDefinition.Metadata = &provider;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const std::array queries{QueryFoundationModule, pg::log::QueryLogModule, QueryModuleRuntime,
                             AdmissionFixture::Query};
    auto result = ModuleManager::Create(declarations, queries, {}, report);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), MakeModuleError(ModuleError::kInvalidDefinition));
    EXPECT_FALSE(report.Handle.IsValid());
}
} // namespace

TEST(ModuleMetadata, WrongOwnerRejectedBeforePublish)
{
    const ModuleMetaProvider provider{.Owner = test::kWrongManualOwner, .Entries = {}};
    ExpectRejected(provider);
}
TEST(ModuleMetadata, NullEntriesRejectedBeforePublish)
{
    constexpr meta::TypeInfo kInfo{.Name = "test", .Id = std::nullopt, .Fields = {}};
    const std::array entries{
        ModuleMetaEntry{.CppType = nullptr, .Info = &kInfo},
        ModuleMetaEntry{.CppType = &typeid(test::ManualType), .Info = nullptr},
    };
    for (const auto& entry : entries)
    {
        ExpectRejected({.Owner = test::kManualModuleId, .Entries = std::span(&entry, 1)});
    }
}
TEST(ModuleMetadata, DuplicateRttiAndExplicitIdRejected)
{
    constexpr meta::TypeId kId{.Bytes = {std::byte{1}}};
    constexpr meta::TypeInfo kFirst{.Name = "first", .Id = kId, .Fields = {}};
    constexpr meta::TypeInfo kSecond{.Name = "second", .Id = kId, .Fields = {}};
    const auto originalEntry = test::QueryManualModule()->Metadata->Entries.front();
    // 两项都没有显式 ID，独立验证跨库 RTTI 值重复，避免由 ID 重复掩盖错误。
    const std::array duplicateType{
        originalEntry,
        ModuleMetaEntry{.CppType = &typeid(test::ManualType), .Info = originalEntry.Info},
    };
    const std::array duplicateId{
        ModuleMetaEntry{.CppType = &typeid(int), .Info = &kFirst},
        ModuleMetaEntry{.CppType = &typeid(float), .Info = &kSecond},
    };
    ExpectRejected({.Owner = test::kManualModuleId, .Entries = duplicateType});
    ExpectRejected({.Owner = test::kManualModuleId, .Entries = duplicateId});
}
TEST(ModuleMetadata, DistinctTypesMayOmitExplicitIds)
{
    constexpr meta::TypeInfo kFirst{.Name = "first", .Id = std::nullopt, .Fields = {}};
    constexpr meta::TypeInfo kSecond{.Name = "second", .Id = std::nullopt, .Fields = {}};
    const std::array entries{
        ModuleMetaEntry{.CppType = &typeid(int), .Info = &kFirst},
        ModuleMetaEntry{.CppType = &typeid(float), .Info = &kSecond},
    };
    const ModuleMetaProvider provider{.Owner = test::kManualModuleId, .Entries = entries};
    AdmissionFixture::gDefinition = *test::QueryManualModule();
    AdmissionFixture::gDefinition.Metadata = &provider;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const std::array queries{QueryFoundationModule, pg::log::QueryLogModule, QueryModuleRuntime,
                             AdmissionFixture::Query};
    auto manager = ModuleManager::Create(declarations, queries, {}, report);
    ASSERT_TRUE(manager);
    const auto handle = manager->Find(test::kManualModuleId).value_or(ModuleHandle{});
    ASSERT_TRUE(handle.IsValid());
    ASSERT_TRUE(manager->Register(handle, report));
    ASSERT_TRUE(manager->Activate(handle, report));
    auto borrow = detail::ModuleMetadataAccess::Acquire(*manager, test::kManualModuleId, typeid(float));
    ASSERT_TRUE(borrow);
    EXPECT_EQ(borrow->Get()->Name, "second");
    borrow->Release();
    ASSERT_TRUE(manager->BeginShutdown(report));
    EXPECT_TRUE(manager->TryFinishShutdown(report));
}
TEST(ModuleMetadata, AdmissionAndLookupErrorOrder)
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    const std::array queries{QueryFoundationModule, pg::log::QueryLogModule, QueryModuleRuntime,
                             test::QueryManualModule};
    auto manager = ModuleManager::Create(declarations, queries, {}, report);
    ASSERT_TRUE(manager);
    const auto acquire = [&](ModuleId owner, const std::type_info& type)
    { return detail::ModuleMetadataAccess::Acquire(*manager, owner, type); };
    ExpectError(acquire(test::kWrongManualOwner, typeid(int)), MakeModuleError(ModuleError::kNotFound));
    ExpectError(acquire(test::kManualModuleId, typeid(int)), MakeModuleError(ModuleError::kNotReady));
    const auto handle = manager->Find(test::kManualModuleId).value_or(ModuleHandle{});
    ASSERT_TRUE(handle.IsValid());
    ASSERT_TRUE(manager->Register(handle, report));
    ExpectError(acquire(test::kManualModuleId, typeid(int)), MakeModuleError(ModuleError::kNotReady));
    ASSERT_TRUE(manager->Activate(handle, report));
    ExpectError(acquire(test::kManualModuleId, typeid(int)), MakeModuleError(ModuleError::kNotFound));
    auto borrow = acquire(test::kManualModuleId, typeid(test::ManualType));
    ASSERT_TRUE(borrow);
    EXPECT_EQ(borrow->Get()->Name, "pg::test::ManualType");
    ASSERT_TRUE(manager->BeginShutdown(report));
    ExpectError(acquire(test::kWrongManualOwner, typeid(int)), MakeModuleError(ModuleError::kInvalidState));
    EXPECT_FALSE(manager->TryFinishShutdown(report));
    borrow->Release();
    ASSERT_TRUE(manager->TryFinishShutdown(report));
    ExpectError(acquire(test::kManualModuleId, typeid(int)), MakeModuleError(ModuleError::kInvalidState));
    const auto moved = std::move(*manager);
    ExpectError(acquire(test::kManualModuleId, typeid(int)), MakeModuleError(ModuleError::kInvalidState));
}
TEST(ModuleMetadata, MissingMetadataRemainsLegal)
{
    AdmissionFixture::gDefinition = *test::QueryManualModule();
    AdmissionFixture::gDefinition.Metadata = nullptr;
    RegistrationScope declarations;
    ModuleOperationReport report;
    const std::array queries{QueryFoundationModule, pg::log::QueryLogModule, QueryModuleRuntime,
                             AdmissionFixture::Query};
    auto manager = ModuleManager::Create(declarations, queries, {}, report);
    ASSERT_TRUE(manager);
    const auto handle = manager->Find(test::kManualModuleId).value_or(ModuleHandle{});
    ASSERT_TRUE(handle.IsValid());
    ExpectError(detail::ModuleMetadataAccess::Acquire(*manager, test::kManualModuleId, typeid(int)),
                MakeModuleError(ModuleError::kNotReady));
    ASSERT_TRUE(manager->Register(handle, report));
    ASSERT_TRUE(manager->Activate(handle, report));
    ExpectError(detail::ModuleMetadataAccess::Acquire(*manager, test::kManualModuleId, typeid(int)),
                MakeModuleError(ModuleError::kNotFound));
    ASSERT_TRUE(manager->BeginShutdown(report));
    EXPECT_TRUE(manager->TryFinishShutdown(report));
}
TEST(ModuleMetadata, ReentryBusyPrecedesMissingOwnerAndInactiveRecord)
{
    AdmissionFixture::gDefinition = *test::QueryManualModule();
    AdmissionFixture::gDefinition.RegisterModule = AdmissionFixture::Register;
    AdmissionFixture::gReentryError.reset();
    RegistrationScope declarations;
    ModuleOperationReport report;
    const std::array queries{QueryFoundationModule, pg::log::QueryLogModule, QueryModuleRuntime,
                             AdmissionFixture::Query};
    auto manager = ModuleManager::Create(declarations, queries, {}, report);
    ASSERT_TRUE(manager);
    AdmissionFixture::gManager = &*manager;
    const auto handle = manager->Find(test::kManualModuleId).value_or(ModuleHandle{});
    ASSERT_TRUE(handle.IsValid());
    ASSERT_TRUE(manager->Register(handle, report));
    EXPECT_EQ(AdmissionFixture::gReentryError, MakeModuleError(ModuleError::kBusy));
    AdmissionFixture::gManager = nullptr;
    ASSERT_TRUE(manager->BeginShutdown(report));
    EXPECT_TRUE(manager->TryFinishShutdown(report));
}
TEST(ModuleMetadata, BorrowPinsCorrectBusinessModuleUntilExplicitRelease)
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    for (const auto handle : manager->RegistrationOrder())
    {
        ASSERT_TRUE(manager->Register(handle, report));
        ASSERT_TRUE(manager->Activate(handle, report));
    }
#ifdef PG_TEST_MODULAR
    const std::array<ModuleLoadCandidate, 1> candidates{
        SharedModuleCandidate{.ExpectedId = test::kManualModuleId, .Path = test::kManualModulePath},
    };
#else
    const std::array<ModuleLoadCandidate, 1> candidates{StaticModuleCandidate{test::QueryManualModule}};
#endif
    auto loaded = manager->Load(candidates, {}, report);
    ASSERT_TRUE(loaded);
    const auto handle = loaded->Modules.front();
    ASSERT_TRUE(manager->Register(handle, report));
    ASSERT_TRUE(manager->Activate(handle, report));
    auto first = detail::ModuleMetadataAccess::Acquire(*manager, test::kManualModuleId, typeid(test::ManualType));
    auto second = detail::ModuleMetadataAccess::Acquire(*manager, test::kManualModuleId, typeid(test::ManualType));
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    *first = std::move(*second);
    EXPECT_FALSE(*second);
    EXPECT_EQ(second->Get(), nullptr);
    ASSERT_TRUE(manager->BeginUnload(handle, report));
    ExpectError(detail::ModuleMetadataAccess::Acquire(*manager, test::kManualModuleId, typeid(int)),
                MakeModuleError(ModuleError::kNotReady));
    const auto blocked = manager->TryFinishUnload(handle, report);
    ASSERT_FALSE(blocked);
    EXPECT_EQ(blocked.error(), MakeModuleError(ModuleError::kBusy));
    EXPECT_EQ(first->Get()->Name, "pg::test::ManualType");
    first->Release();
    EXPECT_TRUE(manager->Inspect(handle));
    ASSERT_TRUE(manager->TryFinishUnload(handle, report));
    EXPECT_FALSE(manager->Find(test::kManualModuleId));
    ASSERT_TRUE(manager->BeginShutdown(report));
    EXPECT_TRUE(manager->TryFinishShutdown(report));
}
} // namespace pg::module
