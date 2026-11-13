#pragma once

#include "LogDrain.h"
#include "LogFileStore.h"
#include "LogQueue.h"
#include "LogTestAccess.h"
#include "NativeLogThread.h"
#include "SpdlogBackend.h"
#include "WriterControl.h"

#include <LogSystem.h>

#include <optional>
#include <span>

namespace pg::log::detail
{
struct LogSystemData
{
    explicit LogSystemData(LogSystem& owner, LogTestHooks* hooks) noexcept
        : Owner(owner)
        , Hooks(hooks)
        , Console(hooks != nullptr ? &hooks->Console : nullptr)
        , File(hooks != nullptr ? &hooks->File : nullptr)
    {
    }
    static void Run(void* context) noexcept;
    void Consume() noexcept;
    void CaptureProducers(LogSnapshot& snapshot) const noexcept;
    [[nodiscard]] bool TryCaptureProducers(LogSnapshot& snapshot) const noexcept;
    void MergeProducers(LogSnapshot& snapshot, const QueueSnapshot& queue) const noexcept;
    void RecordOutcomes(const std::array<OutputOutcome, 2>& outcomes, bool flush) noexcept;
    [[nodiscard]] bool CloseOutputs() noexcept;
    [[nodiscard]] OutputPort Port(std::size_t index) noexcept;
    [[nodiscard]] OutputOutcome WriteOutput(std::size_t index, std::string_view text) noexcept;
    [[nodiscard]] OutputOutcome FlushOutput(std::size_t index) noexcept;
    void EmitControlNotice() noexcept;
    LogSystem& Owner;
    LogTestHooks* Hooks{};
    LogConfig Config;
    LogPath Directory;
    LogQueue Queue;
    WriterControlOwner Writer;
    NativeLogThread Worker;
    NativeLogIo Console;
    FileStore File;
    std::optional<SpdlogBackend> Backend;
    std::span<LogRecord> Records;
    LogRecord Record;
    EncodedLogLine Line;
    LogSnapshot Working;
    LogDrain Drain;
    std::atomic<bool> Published;
    std::atomic<bool> StopRequested;
    std::atomic<bool> RetryCleanup;
    std::atomic<std::uint64_t> EncodingTruncated;
    std::atomic<bool> EncodingOverflow;
    bool ControlOwned{};
    bool Finalized{};
    bool CleanupOnly{};
    bool NativeReaped{};
};
} // namespace pg::log::detail
