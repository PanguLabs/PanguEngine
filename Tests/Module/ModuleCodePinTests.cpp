#include "ModuleOperation.h"
#include <ModuleBuiltin.h>
#include <ModuleCodePin.h>
#include <ModuleError.h>
#include <ModuleManager.h>
#include <RegistrationContext.h>
#include <expected>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#ifdef _WIN32
#include <Windows.h> // IWYU pragma: keep
#include <errhandlingapi.h>
#include <winnt.h>
#else
#include <csignal>
#include <sys/wait.h>
#endif
#include <type_traits>
#include <utility>

namespace pg::module
{
namespace
{
bool IsExpectedPinTermination(int status)
{
#ifdef _WIN32
    return static_cast<std::uint32_t>(status) == 0xc0000409U;
#else
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL;
#endif
}
[[noreturn]] void UnexpectedCrashControl()
{
    std::fputs("pin-control-ready\n", stderr);
    std::fflush(stderr);
#ifdef _WIN32
    (void)SetErrorMode(SEM_NOGPFAULTERRORBOX);
    // 避开 GTest 主线程的 SEH 转换，观察真正的访问违规终止状态。
    std::thread crashing(
        []
        {
            RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
            std::_Exit(91);
        });
    crashing.join();
#else
    std::raise(SIGSEGV);
#endif
    std::_Exit(91);
}
bool IsRejectedCrashControl(int status)
{
#ifdef _WIN32
    const bool unexpectedCrash = static_cast<std::uint32_t>(status) == 0xc0000005U;
#else
    const bool unexpectedCrash = WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV;
#endif
    return unexpectedCrash && !IsExpectedPinTermination(status);
}
} // namespace

TEST(ModuleCodePin, FatalPredicateRejectsPreparationFailureAndUnexpectedCrash)
{
#ifdef _WIN32
    for (const int status : {0, 42, 90, 91})
    {
        EXPECT_FALSE(IsExpectedPinTermination(status));
    }
    EXPECT_FALSE(IsExpectedPinTermination(static_cast<int>(0xc0000005U)));
    EXPECT_TRUE(IsExpectedPinTermination(static_cast<int>(0xc0000409U)));
#else
    for (const int code : {0, 42, 90, 91})
    {
        EXPECT_FALSE(IsExpectedPinTermination(code << 8));
    }
    EXPECT_FALSE(IsExpectedPinTermination(11));
    EXPECT_TRUE(IsExpectedPinTermination(9));
#endif
}

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
            std::fputs("pin-manager-ready\n", stderr);
            std::fflush(stderr);
            manager = std::unexpected(MakeModuleError(ModuleError::kInvalidState));
            std::_Exit(91);
        },
        IsExpectedPinTermination, "pin-manager-ready");
}
TEST(ModuleCodePin, RealPreparationFailureAndUnrelatedCrashAreRejected)
{
    for (const int code : {90, 91})
    {
        EXPECT_EXIT(
            std::_Exit(code), [code](int status)
            { return ::testing::ExitedWithCode(code)(status) && !IsExpectedPinTermination(status); }, "");
    }
    EXPECT_EXIT(UnexpectedCrashControl(), IsRejectedCrashControl, "pin-control-ready");
}
} // namespace pg::module
