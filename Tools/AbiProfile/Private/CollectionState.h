#pragma once

#include "CollectionRequest.h"
#include "CompileCommands.h"
#include "ToolIo.h"

#include <filesystem>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace pg::abi
{
struct CompileObservation
{
    nlohmann::json Facts = nlohmann::json::object();
    std::vector<std::filesystem::path> Headers;
    std::vector<std::filesystem::path> ObservedInputs;
};

struct CollectedProfile
{
    nlohmann::json Profile;
    nlohmann::json Evidence;
    std::vector<std::filesystem::path> ObservedInputs;
};

struct InputSnapshot
{
    nlohmann::json Files = nlohmann::json::array();
    nlohmann::json Context;
    nlohmann::json Environment;
    std::string CompileDigest;
    std::string DependencyDigest;
    std::string Digest;
};

// Tool-private I/O boundary: all transaction writes, publication and rollback
// propagate the same checked filesystem operations (also usable by fault tests).
struct CollectionIo
{
    decltype(&WriteFile) Write = &WriteFile;
    decltype(&RemoveGeneratedFile) Remove = &RemoveGeneratedFile;
    Status (*Publish)(const std::filesystem::path&, const std::filesystem::path&, FailureReport&) = nullptr;
};

enum class GuardDisposition : std::uint8_t
{
    kCurrent,
    kBuildRequired,
    kRecollectRequired,
};

// Owns sorted member locks throughout observation, collection and publication.
// The report must outlive the transaction; requests are copied into its state.
class CollectionTransaction
{
public:
    struct State;
    explicit CollectionTransaction(std::unique_ptr<State> state);
    ~CollectionTransaction();
    CollectionTransaction(const CollectionTransaction&) = delete;
    CollectionTransaction& operator=(const CollectionTransaction&) = delete;
    CollectionTransaction(CollectionTransaction&&) = delete;
    CollectionTransaction& operator=(CollectionTransaction&&) = delete;
    [[nodiscard]] Status Publish(std::span<const CollectedProfile> profiles, std::span<const InputSnapshot> snapshots);

private:
    std::unique_ptr<State> Data;
};
[[nodiscard]] Result<std::unique_ptr<CollectionTransaction>>
BeginCollectionTransaction(std::span<const CollectionRequest> requests, FailureReport& report,
                           const CollectionIo& io = {});

[[nodiscard]] Result<InputSnapshot> SnapshotInputs(const CollectionRequest& request, std::span<const CompileUnit> units,
                                                   const CompileObservation& observation, FailureReport& report);
[[nodiscard]] Result<GuardDisposition> GuardCollection(const CollectionRequest& request, FailureReport& report);
[[nodiscard]] Status InvalidateCollection(const CollectionRequest& request, FailureReport& report);
[[nodiscard]] Status PublishCollection(const CollectionRequest& request, const CollectedProfile& profile,
                                       const InputSnapshot& snapshot, FailureReport& report,
                                       const CollectionIo& io = {});
// All inputs must describe exactly one resolved technical SCC. No completion
// is usable until all receipts/profiles pass and the group seal is published.
[[nodiscard]] Status PublishCollectionGroup(std::span<const CollectionRequest> requests,
                                            std::span<const CollectedProfile> profiles,
                                            std::span<const InputSnapshot> snapshots, FailureReport& report,
                                            const CollectionIo& io = {});
[[nodiscard]] int RunObservedCompilerInvocation(const CollectionRequest& request, const CompileUnit& unit,
                                                const CompileObservation& observation, FailureReport& report,
                                                std::stop_token cancellation = {});
[[nodiscard]] std::filesystem::path CompileReceiptPath(const CollectionRequest& request, const CompileUnit& unit);
} // namespace pg::abi
