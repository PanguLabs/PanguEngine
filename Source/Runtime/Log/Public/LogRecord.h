#pragma once

#include <LogTypes.h>
#include <PanguLogExport.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace pg::log
{
inline constexpr std::size_t kLogMessageCapacity = 1024;
inline constexpr std::size_t kLogCategoryCapacity = 64;
inline constexpr std::size_t kLogLocationCapacity = 512;
inline constexpr std::size_t kEncodedLogLineCapacity = 12UZ * 1024;
inline constexpr std::size_t kLogFileHeaderCapacity = 1024;

namespace detail
{
// 只扫描给定范围；输出保留完整 UTF-8 字符与控制字节转义。
[[nodiscard]] PG_LOG_API std::size_t CopyLogText(std::span<char> destination, std::string_view text,
                                                 bool& truncated) noexcept;
} // namespace detail

template <std::size_t Capacity>
class FixedLogText final
{
public:
    void Assign(std::string_view text, bool& truncated) noexcept { Used = detail::CopyLogText(Bytes, text, truncated); }
    [[nodiscard]] std::string_view Text() const noexcept { return {Bytes.data(), Used}; }

private:
    std::array<char, Capacity> Bytes{};
    std::size_t Used{};
};

// 默认复制/移动只复制值与内联字节，不保存原始 Module 的文本或格式化代码。
struct LogRecord
{
    std::uint64_t Sequence{};
    Level Severity{Level::kInfo};
    LogTags Tags;
    std::optional<Error> Failure;
    std::uint32_t SourceLine{};
    std::uint32_t SourceColumn{};
    bool Truncated{};
    bool ConstructionFailed{};
    FixedLogText<kLogMessageCapacity> MessageText;
    FixedLogText<kLogCategoryCapacity> CategoryText;
    // 文件和函数各占一半；两者总文本预算始终为 512 B。
    FixedLogText<kLogLocationCapacity / 2> FileText;
    FixedLogText<kLogLocationCapacity / 2> FunctionText;

    [[nodiscard]] std::string_view Message() const noexcept { return MessageText.Text(); }
    [[nodiscard]] std::string_view Category() const noexcept { return CategoryText.Text(); }
    [[nodiscard]] std::string_view File() const noexcept { return FileText.Text(); }
    [[nodiscard]] std::string_view Function() const noexcept { return FunctionText.Text(); }
};

struct EncodedLogLine
{
    std::array<char, kEncodedLogLineCapacity> Bytes{};
    std::size_t Size{};
    bool Truncated{};
    [[nodiscard]] std::string_view Text() const noexcept { return {Bytes.data(), Size}; }
};
} // namespace pg::log
