#include "NativeLogIo.h"
#include "LogRecord.h"
#include "LogTypes.h"
#include "Result.h"
#include <Check.h>
#include <algorithm>
#include <expected>

namespace pg::log::detail
{
NativeLogIo::~NativeLogIo()
{
    [[maybe_unused]] const auto cleanup = Close();
    PG_CHECK(!Handle.Valid(), "日志原生输出仍有未释放资源");
}
Status NativeLogIo::OpenConsole() noexcept
{
    if (Opened)
    {
        return std::unexpected(MakeLogError(LogError::kInvalidState));
    }
    Opened = true;
    Console = true;
    auto handle = OpenConsoleNative();
    if (!handle)
    {
        Failure = handle.error();
        return std::unexpected(*Failure);
    }
    Handle = *handle;
    return {};
}
void NativeLogIo::Adopt(NativeHandle handle) noexcept
{
    PG_CHECK(!Handle.Valid() && !Failure, "日志文件句柄不能覆盖或故障后重开");
    Handle = handle;
    Opened = true;
}
OutputOutcome NativeLogIo::Write(std::string_view text) noexcept
{
    if (Failure)
    {
        return {.Failure = Failure};
    }
    if (!Handle.Valid())
    {
        return {.Failure = MakeLogError(LogError::kInvalidState)};
    }
    if (text.size() > kEncodedLogLineCapacity)
    {
        Failure = MakeLogError(LogError::kInvalidConfig);
        return {.Failure = Failure};
    }
    if ((Faults != nullptr) && Faults->WriteFailure)
    {
        Failure = Faults->WriteFailure;
        return {.Failure = Failure};
    }
    const auto limit = (Faults != nullptr) ? std::min(text.size(), Faults->WriteLimit) : text.size();
    auto outcome = WriteNative(Handle, text.substr(0, limit), Console);
    if (!outcome.Complete || limit != text.size())
    {
        outcome.Complete = false;
        Failure = outcome.Failure.value_or(MakeLogError(LogError::kIoFailed));
        outcome.Failure = Failure;
    }
    return outcome;
}
OutputOutcome NativeLogIo::Flush() noexcept
{
    if (Failure)
    {
        return {.Failure = Failure};
    }
    if (!Handle.Valid())
    {
        return {.Failure = MakeLogError(LogError::kInvalidState)};
    }
    const auto status = (Faults != nullptr) && Faults->FlushFailure ? Status(std::unexpected(*Faults->FlushFailure))
                                                                    : FlushNative(Handle, Console);
    if (!status)
    {
        Failure = status.error();
        return {.Failure = Failure};
    }
    return {.Complete = true};
}
Status NativeLogIo::Close() noexcept
{
    if (!Handle.Valid())
    {
        return {};
    }
    if ((Faults != nullptr) && Faults->CloseFailure && Faults->CloseRetainsResource)
    {
        return std::unexpected(*Faults->CloseFailure);
    }
    const auto status = CloseNative(Handle);
    if (!status)
    {
        return status;
    }
    if ((Faults != nullptr) && Faults->CloseFailure)
    {
        return std::unexpected(*Faults->CloseFailure);
    }
    return {};
}
OutputPort NativeLogIo::Port() noexcept
{
    return {
        .Context = this,
        .Write = [](void* context, std::string_view text) noexcept
        { return static_cast<NativeLogIo*>(context)->Write(text); },
        .Flush = [](void* context) noexcept { return static_cast<NativeLogIo*>(context)->Flush(); },
    };
}
} // namespace pg::log::detail
