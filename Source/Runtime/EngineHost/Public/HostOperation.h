#pragma once

#include <ModuleOperation.h>
#include <cstdint>
#include <optional>

namespace pg::engine
{
enum class HostState : std::uint8_t
{
    kCold = 0,
    kStarting = 1,
    kReady = 2,
    kClosing = 3,
    kClosed = 4,
};
enum class HostOperationStage : std::uint8_t
{
    kNone = 0,
    kResidentAdmission = 1,
    kResidentRegistration = 2,
    kResidentActivation = 3,
    kBusinessAdmission = 4,
    kBusinessRegistration = 5,
    kBusinessActivation = 6,
    kScopeOperation = 7,
    kShutdown = 8,
};
struct HostStartupFailure
{
    HostOperationStage Stage;
    pg::Error Cause;
    std::optional<module::ModuleOperationReport> ModuleReport;
};
struct HostOperationReport
{
    HostState State{HostState::kCold};
    HostOperationStage Stage{HostOperationStage::kNone};
    std::optional<pg::Error> PrimaryError;
    std::optional<module::ModuleOperationReport> ModuleReport;
    std::optional<HostStartupFailure> StartupFailure;
};
} // namespace pg::engine
