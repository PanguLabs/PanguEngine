#include "ModuleOperation.h"
#include <ModuleBuiltin.h>
#include <ModuleCodePin.h>
#include <ModuleError.h>
#include <ModuleManager.h>
#include <RegistrationContext.h>
#include <expected>
#include <gtest/gtest.h>

#include <cstdlib>
#include <exception>
#include <type_traits>
#include <utility>

namespace pg::module
{
static_assert(!std::is_copy_constructible_v<ModuleCodePin>);
static_assert(std::is_nothrow_default_constructible_v<ModuleCodePin>);
static_assert(std::is_nothrow_move_constructible_v<ModuleCodePin>);
static_assert(std::is_nothrow_move_assignable_v<ModuleCodePin>);
static_assert(std::is_nothrow_destructible_v<ModuleCodePin>);

TEST(ModuleCodePin, ActiveAdmissionMoveReleaseAndResidentShutdown)
{
    RegistrationScope declarations;
    ModuleOperationReport report;
    auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
    ASSERT_TRUE(manager);
    const auto handles = manager->Modules();
    for (const auto handle : handles)
    {
        EXPECT_FALSE(manager->TryPinForBusiness(handle));
        ASSERT_TRUE(manager->Register(handle, report));
        EXPECT_FALSE(manager->TryPinForBusiness(handle));
        ASSERT_TRUE(manager->Activate(handle, report));
    }
    auto acquired = manager->TryPinForBusiness(handles.back());
    ASSERT_TRUE(acquired);
    ModuleCodePin pin(std::move(*acquired));
    EXPECT_FALSE(*acquired);
    EXPECT_TRUE(pin);
    ModuleCodePin moved;
    EXPECT_FALSE(moved);
    moved = std::move(pin);
    // 移动后无效是该类型的公开契约。
    EXPECT_FALSE(pin); // NOLINT(bugprone-use-after-move)
    auto second = manager->TryPinForBusiness(handles.back());
    ASSERT_TRUE(second);
    moved = std::move(*second);
    EXPECT_FALSE(*second);
    ASSERT_TRUE(manager->BeginShutdown(report));
    EXPECT_FALSE(manager->TryPinForBusiness(handles.back()));
    const auto busy = manager->TryFinishShutdown(report);
    EXPECT_FALSE(busy);
    EXPECT_TRUE(manager->Inspect(handles.back()));
    moved.Release();
    moved.Release();
    ASSERT_TRUE(manager->TryFinishShutdown(report));
    EXPECT_FALSE(manager->TryPinForBusiness(handles.back()));
}

TEST(ModuleCodePin, ManagerDestructionWithOutstandingPinTerminates)
{
    EXPECT_EXIT(
        {
            std::set_terminate([] { std::_Exit(86); });
            ModuleCodePin pin;
            RegistrationScope declarations;
            ModuleOperationReport report;
            auto manager = ModuleManager::Create(declarations, BuiltinResidentQueries(), {}, report);
            if (!manager)
            {
                std::_Exit(90);
            }
            const auto handle = manager->Modules().front();
            if (!manager->Register(handle, report) || !manager->Activate(handle, report))
            {
                std::_Exit(90);
            }
            auto acquired = manager->TryPinForBusiness(handle);
            if (!acquired)
            {
                std::_Exit(90);
            }
            pin = std::move(*acquired);
            manager = std::unexpected(MakeModuleError(ModuleError::kInvalidState));
            std::_Exit(91);
        },
        ::testing::ExitedWithCode(86), "");
}
} // namespace pg::module
