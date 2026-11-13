#pragma once

#include <Error.h>
#include <ModuleIdentity.h>
#include <PanguHostTestSupportExport.h>
#include <ScopeIdentity.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace pg::engine::test
{
enum class HostTestEvent : std::uint8_t
{
    kFactory,
    kStart,
    kStop,
    kDestroy,
    kDeleter,
    kFactoryDestroy,
    kNativeUnload,
};
struct HostTestEventRecord
{
    HostTestEvent Event{};
    module::ScopeHandle Scope{};
    module::ModuleId Owner{};
};
struct HostTestState
{
    PG_HOST_TEST_SUPPORT_API void Reset() noexcept;
    std::optional<pg::Error> FailRegister;
    std::optional<pg::Error> FailStart;
    std::optional<pg::Error> FailFactory;
    std::optional<pg::Error> PollError;
    std::size_t FactoryCount{};
    std::size_t DestroyCount{};
    std::size_t NativeUnloadCount{};
    std::vector<HostTestEventRecord> Events;
    std::size_t QueryCount{};
    std::size_t RegisterCount{};
    std::size_t PollCount{};
    std::function<void()> OnQuery;
    std::function<void()> OnFactory;
    std::function<void()> OnStop;
    std::function<void()> OnDestroy;
    std::function<void()> OnNativeUnload;
};
[[nodiscard]] PG_HOST_TEST_SUPPORT_API std::shared_ptr<HostTestState> CurrentHostTestState() noexcept;
PG_HOST_TEST_SUPPORT_API void SetHostTestState(std::shared_ptr<HostTestState> state) noexcept;
} // namespace pg::engine::test
