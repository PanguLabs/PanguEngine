#include "ModuleDescriptor.h"
#include "ModuleOperation.h"
#include "Result.h"
#include <ModuleBuiltin.h>
#include <ModuleError.h>
#include <ModuleManager.h>
#include <RegistrationContext.h>
#include <array>
#include <cstddef>
#include <gtest/gtest.h>
#include <utility>

namespace pg::module
{
#ifndef PG_TEST_MODULAR
namespace
{
Status Noop(RegistrationContext& /*unused*/) noexcept { return {}; }
template <std::size_t Index>
const ModuleDefinition* QueryCapacity() noexcept
{
    static constexpr ModuleDefinition kDefinition{
        .Descriptor = {.Id = {.Bytes = {std::byte{0x45}, static_cast<std::byte>(Index)}},
                       .Dependencies = {},
                       .DisplayName = "capacity",
                       .BuildIdentity = "stable"},
        .RegisterModule = Noop};
    return &kDefinition;
}
template <std::size_t... Index>
auto CapacityCandidates(std::index_sequence<Index...> /*unused*/)
{
    return std::array{ModuleLoadCandidate{StaticModuleCandidate{QueryCapacity<Index>}}...};
}
} // namespace
TEST(ModuleManager, ResidentBudgetAndReloadGeneration)
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    const auto candidates = CapacityCandidates(std::make_index_sequence<254>{});
    auto loaded = manager->Load(std::span(candidates).first(253), {}, report);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(manager->Modules().size(), 256U);
    const auto oldHandle = loaded->Modules.front();
    auto snapshot = manager->Inspect(oldHandle);
    const auto overBudget = manager->Load(std::span(candidates).last(1), {}, report);
    ASSERT_FALSE(overBudget);
    EXPECT_EQ(overBudget.error(), MakeModuleError(ModuleError::kLimitExceeded));
    ASSERT_TRUE(manager->BeginUnload(oldHandle, report));
    ASSERT_TRUE(manager->TryFinishUnload(oldHandle, report));
    EXPECT_FALSE(manager->Inspect(oldHandle));
    auto reloaded = manager->Load(std::span(candidates).first(1), {}, report);
    ASSERT_TRUE(reloaded);
    const auto reloadedHandle = reloaded->Modules.front();
    EXPECT_EQ(oldHandle.Slot, reloadedHandle.Slot);
    EXPECT_NE(oldHandle.Generation, reloadedHandle.Generation);
    EXPECT_EQ(snapshot->Descriptor.DisplayName, "capacity");
    {
        const auto rejected = manager->Register(oldHandle, report);
        EXPECT_FALSE(rejected);
        if (!rejected)
        {
            EXPECT_EQ(rejected.error(), MakeModuleError(ModuleError::kInvalidHandle));
        }
    }
}
#endif
TEST(ModuleManager, HandlesBelongToOneStableDomain)
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto first = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    auto second = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    const auto handle = first->Modules().front();
    EXPECT_NE(handle.Domain, second->Modules().front().Domain);
    {
        const auto rejected = second->Inspect(handle);
        EXPECT_FALSE(rejected);
        if (!rejected)
        {
            EXPECT_EQ(rejected.error(), MakeModuleError(ModuleError::kInvalidHandle));
        }
    }
    const auto host = first->HostScope();
    auto const moved = std::move(*first);
    EXPECT_TRUE(moved.Inspect(handle));
    EXPECT_EQ(moved.HostScope(), host);
}
} // namespace pg::module
