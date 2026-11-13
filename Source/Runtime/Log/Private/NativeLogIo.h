#pragma once

#include <LogConfig.h>
#include <PanguLogExport.h>

#include <limits>

namespace pg::log::detail
{
struct OutputOutcome
{
    std::uint64_t BytesWritten{};
    // 部分指定初始化也必须显式得到空 Failure。
    // NOLINTNEXTLINE(readability-redundant-member-init)
    std::optional<Error> Failure{};
    bool Complete{};
    // 文件/重定向为字节，Windows 原生终端为 UTF-16 code units。
    std::uint64_t NativeUnitsWritten{};
};

struct OutputPort
{
    void* Context{};
    OutputOutcome (*Write)(void*, std::string_view) noexcept {};
    OutputOutcome (*Flush)(void*) noexcept {};
};

// 私有、实例内、借用的故障接缝，不允许 Plugin 配置或业务回调。
struct NativeIoFaults
{
    std::optional<Error> WriteFailure;
    std::optional<Error> FlushFailure;
    std::optional<Error> CloseFailure;
    std::optional<Error> CreateFailure;
    std::optional<Error> DeleteFailure;
    std::optional<Error> DeleteCloseFailure;
    std::size_t WriteLimit{std::numeric_limits<std::size_t>::max()};
    bool CloseRetainsResource{};
    bool DeleteCloseRetainsResource{};
};

struct NativeHandle
{
    std::intptr_t Value{-1};
    [[nodiscard]] bool Valid() const noexcept { return Value != -1; }
};

[[nodiscard]] PG_LOG_API Error NativeIoError(std::int64_t code, LogError kind = LogError::kIoFailed) noexcept;
[[nodiscard]] PG_LOG_API OutputOutcome WriteNative(NativeHandle handle, std::string_view text, bool console) noexcept;
[[nodiscard]] PG_LOG_API Status FlushNative(NativeHandle handle, bool console) noexcept;
// 成功或确定已释放时置无效；失败且仍拥有时保留句柄，供串行 Close 重试。
[[nodiscard]] PG_LOG_API Status CloseNative(NativeHandle& handle) noexcept;
[[nodiscard]] PG_LOG_API Result<NativeHandle> OpenConsoleNative() noexcept;

class PG_LOG_API NativeLogIo final
{
public:
    explicit NativeLogIo(NativeIoFaults* faults = nullptr) noexcept
        : Faults(faults)
    {
    }
    ~NativeLogIo();
    NativeLogIo(const NativeLogIo&) = delete;
    NativeLogIo& operator=(const NativeLogIo&) = delete;
    NativeLogIo(NativeLogIo&&) = delete;
    NativeLogIo& operator=(NativeLogIo&&) = delete;
    [[nodiscard]] Status OpenConsole() noexcept;
    void Adopt(NativeHandle handle) noexcept;
    [[nodiscard]] OutputOutcome Write(std::string_view text) noexcept;
    [[nodiscard]] OutputOutcome Flush() noexcept;
    [[nodiscard]] Status Close() noexcept;
    [[nodiscard]] bool OwnsResource() const noexcept { return Handle.Valid(); }
    [[nodiscard]] OutputPort Port() noexcept;

private:
    NativeHandle Handle;
    NativeIoFaults* Faults{};
    std::optional<Error> Failure;
    bool Console{};
    bool Opened{};
};
} // namespace pg::log::detail
