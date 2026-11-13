#include "SpdlogBackend.h"
#include "NativeLogIo.h"

#include <LogTypes.h>
#include <spdlog/common.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/formatter.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>

namespace pg::log::detail
{
RawSink::RawSink(OutputPort port) noexcept
    : Port(port)
{
}
void RawSink::log(const spdlog::details::log_msg& message) noexcept
{
    Last = {};
    if (InvalidConfig)
    {
        Last.Failure = MakeLogError(LogError::kInvalidConfig);
    }
    else if (Port.Write != nullptr)
    {
        Last = Port.Write(Port.Context, {message.payload.data(), message.payload.size()});
    }
}
void RawSink::flush() noexcept
{
    Last = {};
    if (InvalidConfig)
    {
        Last.Failure = MakeLogError(LogError::kInvalidConfig);
    }
    else if (Port.Flush != nullptr)
    {
        Last = Port.Flush(Port.Context);
    }
}
void RawSink::set_pattern(const std::string& /*pattern*/) noexcept { InvalidConfig = true; }
void RawSink::set_formatter(std::unique_ptr<spdlog::formatter> /*formatter*/) noexcept { InvalidConfig = true; }
OutputOutcome RawSink::Outcome() const noexcept { return Last; }

SpdlogBackend::SpdlogBackend(OutputPort console, OutputPort file) noexcept
    : Outputs{RawSink{console}, RawSink{file}}
{
}
std::array<OutputOutcome, 2> SpdlogBackend::Emit(Level level, std::string_view text) noexcept
{
    // 显式时间与 source_loc，避开线程 ID 和格式化消息重载。
    spdlog::level::level_enum backendLevel{};
    switch (level)
    {
    case Level::kTrace:
        backendLevel = spdlog::level::trace;
        break;
    case Level::kDebug:
        backendLevel = spdlog::level::debug;
        break;
    case Level::kInfo:
        backendLevel = spdlog::level::info;
        break;
    case Level::kWarning:
        backendLevel = spdlog::level::warn;
        break;
    case Level::kError:
        backendLevel = spdlog::level::err;
        break;
    case Level::kOff:
    default:
        return {};
    }
    const spdlog::details::log_msg message{spdlog::log_clock::time_point{},
                                           spdlog::source_loc{},
                                           {},
                                           backendLevel,
                                           spdlog::string_view_t{text.data(), text.size()}};
    for (auto& output : Outputs)
    {
        output.log(message);
    }
    return {std::get<0>(Outputs).Outcome(), std::get<1>(Outputs).Outcome()};
}
std::array<OutputOutcome, 2> SpdlogBackend::Flush() noexcept
{
    for (auto& output : Outputs)
    {
        output.flush();
    }
    return {std::get<0>(Outputs).Outcome(), std::get<1>(Outputs).Outcome()};
}
} // namespace pg::log::detail
