#pragma once

#include "NativeLogIo.h"
#include <spdlog/sinks/sink.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace pg::log::detail
{
class RawSink final : public spdlog::sinks::sink
{
public:
    explicit RawSink(OutputPort port) noexcept;
    void log(const spdlog::details::log_msg& message) noexcept override;
    void flush() noexcept override;
    void set_pattern(const std::string& pattern) noexcept override;
    void set_formatter(std::unique_ptr<spdlog::formatter> formatter) noexcept override;
    [[nodiscard]] OutputOutcome Outcome() const noexcept;

private:
    OutputPort Port;
    OutputOutcome Last;
    bool InvalidConfig{};
};

class SpdlogBackend final
{
public:
    SpdlogBackend(OutputPort console, OutputPort file) noexcept;
    SpdlogBackend(const SpdlogBackend&) = delete;
    SpdlogBackend& operator=(const SpdlogBackend&) = delete;
    SpdlogBackend(SpdlogBackend&&) = delete;
    SpdlogBackend& operator=(SpdlogBackend&&) = delete;
    ~SpdlogBackend() = default;

    [[nodiscard]] std::array<OutputOutcome, 2> Emit(Level level, std::string_view text) noexcept;
    [[nodiscard]] std::array<OutputOutcome, 2> Flush() noexcept;

private:
    // 直接消费同步 Sink，避免 logger 空容器在 MSVC Debug 中分配 iterator proxy。
    std::array<RawSink, 2> Outputs;
};
} // namespace pg::log::detail
