#include "ModuleTestAccess.h"
#include "NativeModuleLibrary.h"
#include "SharedFixtureContract.h"
#include "SharedFixturePaths.h"

#include <ModuleDescriptor.h>
#include <ModuleError.h>
#include <ScopeTestSupport.h>
#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <system_error>
#include <utility>

namespace pg::module
{
namespace
{
using detail::NativeModuleLibrary;
namespace fs = std::filesystem;

TEST(NativeModuleLibrary, OpenMissing)
{
    const auto result =
        NativeModuleLibrary::Open(fs::path(fixture::kSharedProviderPath).parent_path() / "absent-library");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().Code(), static_cast<unsigned>(ModuleError::kLibraryOpenFailed));
    EXPECT_EQ(result.error().Domain(), kModuleErrorDomain);
}

TEST(NativeModuleLibrary, RelativePath)
{
    const auto result = NativeModuleLibrary::Open(fs::path(fixture::kSharedProviderPath).filename());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), MakeModuleError(ModuleError::kInvalidPath));
}

TEST(NativeModuleLibrary, MissingQuery)
{
    auto library = NativeModuleLibrary::Open(fixture::kMissingQueryPath);
    ASSERT_TRUE(library);
    const auto query = library->ResolveQuery();
    ASSERT_FALSE(query);
    EXPECT_EQ(query.error().Code(), static_cast<unsigned>(ModuleError::kQuerySymbolMissing));
    // 查询失败不交出清理责任，原对象仍可查找并显式释放。
    EXPECT_FALSE(library->ResolveQuery());
    EXPECT_TRUE(library->Release());
    EXPECT_TRUE(library->Release());
}

TEST(NativeModuleLibrary, NullDefinition)
{
    auto library = NativeModuleLibrary::Open(fixture::kNullQueryPath);
    ASSERT_TRUE(library);
    const auto query = library->ResolveQuery();
    ASSERT_TRUE(query);
    EXPECT_EQ((*query)(), nullptr);
    EXPECT_TRUE(library->Release());
}

TEST(NativeModuleLibrary, NativeHold)
{
    auto& observer = pg::test::Observer();
    observer.Reset();
    auto library = NativeModuleLibrary::Open(fixture::kSharedProviderPath);
    ASSERT_TRUE(library);
    auto extraHold = NativeModuleLibrary::Open(fixture::kSharedProviderPath);
    ASSERT_TRUE(extraHold);
    const auto query = library->ResolveQuery();
    ASSERT_TRUE(query);
    ASSERT_NE((*query)(), nullptr);
    EXPECT_EQ((*query)()->Descriptor.Id, fixture::kSharedProviderId);
    EXPECT_NE((*query)()->RegisterModule, nullptr);
    EXPECT_EQ(observer.RegisterCount(), 0U);
    EXPECT_TRUE(library->Release());
    EXPECT_EQ(observer.NativeUnloadCount(), 0U);
    EXPECT_TRUE(extraHold->Release());
    EXPECT_EQ(observer.NativeUnloadCount(), 1U);
}

TEST(NativeModuleLibrary, ConsumerQuery)
{
    auto& observer = pg::test::Observer();
    observer.Reset();
    auto library = NativeModuleLibrary::Open(fixture::kSharedConsumerPath);
    ASSERT_TRUE(library);
    const auto query = library->ResolveQuery();
    ASSERT_TRUE(query);
    const auto* definition = (*query)();
    ASSERT_NE(definition, nullptr);
    EXPECT_EQ(definition->Descriptor.Id, fixture::kSharedConsumerId);
    ASSERT_EQ(definition->Descriptor.Dependencies.size(), 1U);
    EXPECT_EQ(definition->Descriptor.Dependencies.front().Id, fixture::kSharedProviderId);
    EXPECT_EQ(observer.RegisterCount(), 0U);
    EXPECT_TRUE(library->Release());
    EXPECT_EQ(observer.NativeUnloadCount(), 2U);
}

TEST(NativeModuleLibrary, ExplicitHoldsReleaseInBothDependencyLoadOrders)
{
    auto& observer = pg::test::Observer();
    for (const auto providerFirst : std::array{true, false})
    {
        observer.Reset();
        auto first =
            NativeModuleLibrary::Open(providerFirst ? fixture::kSharedProviderPath : fixture::kSharedConsumerPath);
        ASSERT_TRUE(first);
        auto second =
            NativeModuleLibrary::Open(providerFirst ? fixture::kSharedConsumerPath : fixture::kSharedProviderPath);
        ASSERT_TRUE(second);
        auto& provider = providerFirst ? *first : *second;
        auto& consumer = providerFirst ? *second : *first;
        ASSERT_TRUE(consumer.ResolveQuery());
        EXPECT_TRUE(consumer.Release());
        EXPECT_FALSE(consumer.ResolveQuery());
        EXPECT_TRUE(consumer.Release());
        // OS 可以保留隐式装载组；这里只确认自身持有释放，Provider 仍有效。
        EXPECT_TRUE(provider.ResolveQuery());
        EXPECT_EQ(observer.RegisterCount(), 0U);
        EXPECT_TRUE(provider.Release());
        EXPECT_FALSE(provider.ResolveQuery());
        EXPECT_EQ(observer.NativeUnloadCount(), 2U);
    }
}

TEST(NativeModuleLibrary, UnicodeAbsolutePath)
{
    const fs::path source(fixture::kSharedProviderPath);
    const auto directory = fs::current_path() / fs::path(u8"中文 原生库");
    const auto destination = directory / source.filename();
    std::error_code error;
    fs::create_directories(directory, error);
    ASSERT_FALSE(error) << error.message();
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, error);
    ASSERT_FALSE(error) << error.message();
    auto library = NativeModuleLibrary::Open(destination);
    ASSERT_TRUE(library);
    EXPECT_TRUE(library->ResolveQuery());
    EXPECT_TRUE(library->Release());
    fs::remove(destination, error);
    EXPECT_FALSE(error) << error.message();
    fs::remove(directory, error);
    EXPECT_FALSE(error) << error.message();
}

TEST(NativeModuleLibrary, MoveAndDestructor)
{
    auto& observer = pg::test::Observer();
    observer.Reset();
    {
        auto opened = NativeModuleLibrary::Open(fixture::kSharedProviderPath);
        ASSERT_TRUE(opened);
        NativeModuleLibrary library(std::move(*opened));
        EXPECT_TRUE(opened->Release());
        EXPECT_FALSE(opened->ResolveQuery());
        auto consumer = NativeModuleLibrary::Open(fixture::kSharedConsumerPath);
        ASSERT_TRUE(consumer);
        *consumer = std::move(library);
        EXPECT_EQ(observer.NativeUnloadCount(), 1U);
        // 移动契约保证源对象为空，显式验证它仍可安全释放。
        EXPECT_TRUE(library.Release()); // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        EXPECT_TRUE(consumer->ResolveQuery());
    }
    EXPECT_EQ(observer.NativeUnloadCount(), 2U);
}

TEST(NativeModuleLibrary, FailedCloseRetainsHold)
{
    auto& observer = pg::test::Observer();
    observer.Reset();
    auto library = NativeModuleLibrary::Open(fixture::kSharedProviderPath);
    ASSERT_TRUE(library);
    detail::ModuleManagerTestAccess::FailNextClose(*library);
    const auto closed = library->Release();
    ASSERT_FALSE(closed);
    EXPECT_EQ(closed.error(), MakeModuleError(ModuleError::kLibraryCloseFailed));
    EXPECT_EQ(observer.NativeUnloadCount(), 0U);
    EXPECT_TRUE(library->ResolveQuery());
    EXPECT_TRUE(library->Release());
    EXPECT_EQ(observer.NativeUnloadCount(), 1U);
}

TEST(NativeModuleLibrary, DestructorFailureTerminates)
{
    auto library = NativeModuleLibrary::Open(fixture::kSharedProviderPath);
    ASSERT_TRUE(library);
    EXPECT_DEATH(
        {
            NativeModuleLibrary owned(std::move(*library));
            detail::ModuleManagerTestAccess::FailNextClose(owned);
        },
        "");
}

TEST(NativeModuleLibrary, MoveAssignmentFailureTerminates)
{
    auto destination = NativeModuleLibrary::Open(fixture::kSharedProviderPath);
    auto source = NativeModuleLibrary::Open(fixture::kSharedConsumerPath);
    ASSERT_TRUE(destination);
    ASSERT_TRUE(source);
    EXPECT_DEATH(
        {
            detail::ModuleManagerTestAccess::FailNextClose(*destination);
            *destination = std::move(*source);
        },
        "");
}
} // namespace
} // namespace pg::module
