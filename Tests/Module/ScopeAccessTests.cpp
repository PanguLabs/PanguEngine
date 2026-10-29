#include "Result.h"
#include <ModuleError.h>
#include <ScopeAccess.h>
#include <ScopeFactory.h>
#include <ScopeIdentity.h>

#include <array>
#include <atomic>
#include <expected>
#include <iostream>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>

namespace
{
using namespace pg::module;
static_assert(!std::is_copy_constructible_v<ScopeFactory>);
static_assert(std::is_nothrow_move_constructible_v<ScopeFactory>);
static_assert(!std::is_copy_constructible_v<ScopeBorrow<int>>);
static_assert(std::is_nothrow_move_constructible_v<ScopeBorrow<int>>);
static_assert(!ScopeHandle{}.IsValid());

struct Fixture
{
    std::unique_ptr<int> Value = std::make_unique<int>(42);
    std::shared_ptr<detail::ScopeObjectControl> Control = std::make_shared<detail::ScopeObjectControl>();
    Fixture()
    {
        Control->Module = std::make_shared<detail::ScopeGate>();
        Control->Scope = std::make_shared<detail::ScopeGate>();
        Control->Object = Value.get();
        Control->Type = detail::ScopeTypeToken<int>();
        Control->Module->Open();
        Control->Scope->Open();
        Control->Gate.Open();
    }
    [[nodiscard]] ScopeAccess<int> Access() const
    {
        const auto result = detail::ScopeAccessMaker::Make<int>(Control);
        return result ? *result : ScopeAccess<int>{};
    }
};

bool BorrowPinsAndReleasesOnce()
{
    Fixture const fixture;
    auto result = fixture.Access().TryBorrow();
    if (!result || **result != 42 || fixture.Control->Gate.Count() != 1 || fixture.Control->Module->Count() != 1 ||
        fixture.Control->Scope->Count() != 1)
    {
        return false;
    }
    auto borrow = std::move(*result);
    borrow.Release();
    borrow.Release();
    return fixture.Control->Gate.Count() == 0 && fixture.Control->Module->Count() == 0 &&
           fixture.Control->Scope->Count() == 0;
}

bool EveryGateRejectsNewBorrowAndPreservesOldBorrow()
{
    for (int kind = 0; kind < 3; ++kind)
    {
        Fixture const fixture;
        const auto access = fixture.Access();
        auto admitted = access.TryBorrow();
        if (!admitted)
        {
            return false;
        }
        if (kind == 0)
        {
            fixture.Control->Module->Close();
        }
        else if (kind == 1)
        {
            fixture.Control->Scope->Close();
        }
        else
        {
            fixture.Control->Gate.Close();
        }
        const auto rejected = access.TryBorrow();
        if (rejected || rejected.error() != MakeModuleError(ModuleError::kNotReady) ||
            fixture.Control->Gate.Count() != 1 || **admitted != 42)
        {
            return false;
        }
        admitted->Release();
        if (fixture.Control->Gate.Count() != 0 || fixture.Control->Module->Count() != 0 ||
            fixture.Control->Scope->Count() != 0)
        {
            return false;
        }
        fixture.Control->Object = nullptr;
        if (access.TryBorrow())
        {
            return false;
        }
    }
    return true;
}

bool RejectsEmptyAndWrongType()
{
    const auto empty = ScopeAccess<int>{}.TryBorrow();
    Fixture const fixture;
    const auto mismatch = detail::ScopeAccessMaker::Make<double>(fixture.Control);
    return !empty && empty.error() == MakeModuleError(ModuleError::kNotReady) && !mismatch &&
           mismatch.error() == MakeModuleError(ModuleError::kTypeMismatch);
}

bool AcquisitionAndCloseAreLinearized()
{
    for (int round = 0; round < 200; ++round)
    {
        Fixture const fixture;
        const auto access = fixture.Access();
        std::atomic<bool> start{false};
        std::atomic<bool> closed{false};
        std::atomic<bool> failed{false};
        std::jthread reader(
            [&]
            {
                while (!start.load())
                {
                    std::this_thread::yield();
                }
                auto before = access.TryBorrow();
                if (before && **before != 42)
                {
                    failed = true;
                }
                while (!closed.load())
                {
                    std::this_thread::yield();
                }
                if (access.TryBorrow())
                {
                    failed = true;
                }
            });
        start = true;
        fixture.Control->Gate.Close();
        closed = true;
        reader.join();
        if (failed || fixture.Control->Gate.Count() != 0 || fixture.Control->Module->Count() != 0 ||
            fixture.Control->Scope->Count() != 0)
        {
            return false;
        }
    }
    return true;
}

bool FactoryOwnsMoveOnlyCapture()
{
    auto configuration = std::make_unique<int>(7);
    ScopeFactory factory(
        [value = std::move(configuration)](ScopeBuildContext& /*context*/) noexcept -> pg::Status
        { return *value == 7 ? pg::Status{} : std::unexpected(MakeModuleError(ModuleError::kInvalidState)); });
    if (configuration || !factory)
    {
        return false;
    }
    ScopeFactory const moved(std::move(factory));
    // The moved-from owning wrapper is explicitly valid and empty.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    return !factory && static_cast<bool>(moved);
}
} // namespace

int main()
{
    const std::array tests{
        std::pair{"borrow pins and releases once", BorrowPinsAndReleasesOnce},
        std::pair{"every gate rejects new borrow and preserves old borrow",
                  EveryGateRejectsNewBorrowAndPreservesOldBorrow},
        std::pair{"rejects empty and wrong type", RejectsEmptyAndWrongType},
        std::pair{"acquisition and close are linearized", AcquisitionAndCloseAreLinearized},
        std::pair{"factory owns move-only capture", FactoryOwnsMoveOnlyCapture},
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
