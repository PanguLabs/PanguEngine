#pragma once

#include "CollectionState.h"
#include "Process.h"
#include <Result.h>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pg::abi
{
struct CollectionRequest;
struct CompileUnit;
struct FailureReport;

// Process-owned lock; kernel releases ownership even on abnormal process exit.
// Never acquire collection state while owning this lock.
class ObservationWork
{
public:
    ~ObservationWork();
    ObservationWork(const ObservationWork&) = delete;
    ObservationWork& operator=(const ObservationWork&) = delete;
    ObservationWork(ObservationWork&&) = delete;
    ObservationWork& operator=(ObservationWork&&) = delete;
    [[nodiscard]] static Result<std::unique_ptr<ObservationWork>>
    Acquire(const CollectionRequest& request, std::span<const CompileUnit> units,
            std::span<const std::filesystem::path> knownInputs, std::string_view stage, FailureReport& report);
    [[nodiscard]] const std::filesystem::path& Directory() const noexcept { return PrivateDirectory; }
    [[nodiscard]] Result<CompileObservation> Finish(CompileObservation observation);

private:
    ObservationWork() = default;
    [[nodiscard]] Status Cleanup();
    std::filesystem::path PrivateDirectory;
    std::filesystem::path LogicalDirectory;
    const CollectionRequest* Request = nullptr;
    FailureReport* Report = nullptr;

#ifdef _WIN32
    void* Handle = nullptr;
#else
    int Descriptor = -1;
#endif
};

struct TranslationUnitObservation
{
    ProcessResult Macros;
    ProcessResult Driver;
};
[[nodiscard]] Result<ProcessResult> ExecuteObservation(const std::filesystem::path& program,
                                                       const std::filesystem::path& directory,
                                                       std::vector<std::string> arguments, std::string_view stage,
                                                       FailureReport& report);
[[nodiscard]] Status CheckObservationOutput(const std::filesystem::path& path, FailureReport& report);
// Source remains the actual top-level input. VFS changes only its appended tail,
// preserving file identity, include depth and relative header lookup.
[[nodiscard]] Result<TranslationUnitObservation>
ObserveTranslationUnit(const CollectionRequest& request, const CompileUnit& unit, const std::filesystem::path& probe,
                       const std::filesystem::path& directory, std::size_t index, std::string_view stage,
                       FailureReport& report);
} // namespace pg::abi
