#include <Error.h>
#include <LogConfig.h>
#include <LogFormat.h>
#include <LogRecord.h>
#include <LogTypes.h>
#include <ModuleIdentity.h>
#include <Result.h>

#include <fmt/base.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <limits>
#include <optional>
#include <source_location>
#include <span>
#include <string_view>

namespace
{
[[nodiscard]] unsigned int Byte(char value) noexcept { return static_cast<unsigned char>(value); }

[[nodiscard]] std::size_t Utf8ExpectedLength(unsigned int first) noexcept
{
    if (first >= 0xc2 && first <= 0xdf)
    {
        return 2;
    }
    if (first >= 0xe0 && first <= 0xef)
    {
        return 3;
    }
    if (first >= 0xf0 && first <= 0xf4)
    {
        return 4;
    }
    return 0;
}

// 0 表示无效字节；拒绝 overlong、surrogate 与超出 Unicode 范围的编码。
[[nodiscard]] std::size_t Utf8Unit(std::string_view text) noexcept
{
    const auto first = Byte(text.front());
    if (first < 0x80)
    {
        return 1;
    }
    const auto length = Utf8ExpectedLength(first);
    if (length == 0 || text.size() < length)
    {
        return 0;
    }
    for (const auto value : text.substr(1, length - 1))
    {
        if (Byte(value) < 0x80 || Byte(value) > 0xbf)
        {
            return 0;
        }
    }
    const auto second = Byte(text.substr(1).front());
    if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) || (first == 0xf0 && second < 0x90) ||
        (first == 0xf4 && second >= 0x90))
    {
        return 0;
    }
    return length;
}

[[nodiscard]] std::size_t CompletePrefix(std::string_view text, std::size_t limit) noexcept
{
    std::size_t used{};
    while (used < text.size() && used < limit)
    {
        const auto remaining = text.substr(used);
        const auto first = Byte(remaining.front());
        const auto expected = std::max(std::size_t{1}, Utf8ExpectedLength(first));
        if (expected > remaining.size())
        {
            break;
        }
        const auto length = std::max(std::size_t{1}, Utf8Unit(remaining));
        if (length > limit - used)
        {
            break;
        }
        used += length;
    }
    return used;
}

[[nodiscard]] unsigned int UnicodeControl(std::string_view text, std::size_t unit) noexcept
{
    if (unit == 2 && Byte(text.front()) == 0xc2)
    {
        const auto second = Byte(text.substr(1).front());
        if (second >= 0x80 && second <= 0x9f)
        {
            return second;
        }
    }
    if (unit == 3 && Byte(text.front()) == 0xe2 && Byte(text.substr(1).front()) == 0x80)
    {
        const auto third = Byte(text.substr(2).front());
        if (third == 0xa8 || third == 0xa9)
        {
            return 0x2000U + third - 0x80U;
        }
    }
    return 0;
}

[[nodiscard]] std::string_view BoundedSource(const char* text, bool& truncated) noexcept
{
    if (text == nullptr)
    {
        return {};
    }
    // source_location 提供以零结尾的标准字符串；仍给每次扫描设定硬上限。
    std::size_t size{};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic) 标准零结尾字符串，且扫描有硬上限。
    while (size <= pg::log::kLogLocationCapacity && text[size] != '\0')
    {
        ++size;
    }
    truncated = truncated || size > pg::log::kLogLocationCapacity;
    return {text, std::min(size, pg::log::kLogLocationCapacity)};
}

[[nodiscard]] bool IsAbsolute(pg::log::NativePathView path) noexcept
{
    if (path.empty())
    {
        return false;
    }
#ifdef _WIN32
    const auto separator = [](auto value) { return value == L'\\' || value == L'/'; };
    // 与 Win32 文件路径接缝一致：支持盘符根与 UNC；拒绝 drive-relative 和单根相对路径。
    if (path.size() < 3)
    {
        return false;
    }
    const auto first = path.front();
    const auto second = path.substr(1).front();
    const auto third = path.substr(2).front();
    return (((first >= L'A' && first <= L'Z') || (first >= L'a' && first <= L'z')) && second == L':' &&
            separator(third)) ||
           (path.size() >= 5 && separator(first) && separator(second) && !separator(third));
#else
    return path.front() == '/';
#endif
}

[[nodiscard]] bool AddSize(std::size_t& value, std::size_t addition) noexcept
{
    if (addition > std::numeric_limits<std::size_t>::max() - value)
    {
        return false;
    }
    value += addition;
    return true;
}
} // namespace

namespace pg::log
{
Result<std::size_t> RequiredLogMemory(const LogConfig& config, LogMemoryLayout layout) noexcept
{
    const auto invalid = std::unexpected(MakeLogError(LogError::kInvalidConfig));
    // 粗粒度下限按编码容量与两份记录计；真实布局仍须计入编码对象元数据和全部固定资源。
    constexpr auto kMinimumFixed = kEncodedLogLineCapacity + (2 * sizeof(LogRecord));
    if (layout.QueueSlotBytes < sizeof(LogRecord) || layout.FixedBytes < kMinimumFixed ||
        config.QueueCapacity > std::numeric_limits<std::size_t>::max() / layout.QueueSlotBytes)
    {
        return invalid;
    }
    auto bytes = config.QueueCapacity * layout.QueueSlotBytes;
    if (!AddSize(bytes, layout.FixedBytes) || !AddSize(bytes, config.WorkerStackBytes))
    {
        return invalid;
    }
    return bytes;
}

Status ValidateLogConfig(const LogConfig& config, LogMemoryLayout layout) noexcept
{
    const auto invalid = std::unexpected(MakeLogError(LogError::kInvalidConfig));
    constexpr auto kAll = OutputMask::kConsole | OutputMask::kFile;
    if ((config.Enabled | kAll) != kAll || (config.Required | kAll) != kAll ||
        (config.Enabled & config.Required) != config.Required || config.Minimum > Level::kOff ||
        config.QueueCapacity == 0 || config.QueueCapacity > kMaxLogQueueCapacity ||
        config.PriorityReserve >= config.QueueCapacity || config.PriorityReserve > kMaxLogPriorityReserve ||
        config.WorkerStackBytes == 0 || config.DefaultWait.count() < 0)
    {
        return invalid;
    }
    const auto memory = RequiredLogMemory(config, layout);
    if (!memory || *memory > config.MemoryBudgetBytes)
    {
        return invalid;
    }
    return {};
}

Status ValidateLogFileConfig(const LogConfig& config) noexcept
{
    const auto invalid = std::unexpected(MakeLogError(LogError::kInvalidConfig));
    if (HasOutput(config.Enabled, OutputMask::kFile) &&
        (config.Directory.size() > kLogPathCapacity || !IsAbsolute(config.Directory) ||
         std::ranges::find(config.Directory, std::filesystem::path::value_type{}) != config.Directory.end() ||
         std::ranges::all_of(config.PartitionId, [](auto value) { return value == std::byte{}; }) ||
         config.FileBytes < kLogFileHeaderCapacity + kEncodedLogLineCapacity ||
         config.PartitionBytes < config.FileBytes))
    {
        return invalid;
    }
    return {};
}
} // namespace pg::log

namespace pg::log::detail
{
std::size_t CopyLogText(std::span<char> destination, std::string_view text, bool& truncated) noexcept
{
    constexpr std::string_view kHex = "0123456789abcdef";
    std::size_t used{};
    while (!text.empty())
    {
        const auto byte = Byte(text.front());
        const auto unit = Utf8Unit(text);
        const auto unicodeControl = UnicodeControl(text, unit);
        std::array<char, 6> escape{'\\', 'x', '0', '0', '0', '0'};
        std::string_view part;
        std::size_t consumed = std::max(std::size_t{1}, unit);
        if (unicodeControl != 0)
        {
            std::get<1>(escape) = 'u';
            std::get<2>(escape) = kHex.substr((unicodeControl >> 12U) & 0xfU, 1).front();
            std::get<3>(escape) = kHex.substr((unicodeControl >> 8U) & 0xfU, 1).front();
            std::get<4>(escape) = kHex.substr((unicodeControl >> 4U) & 0xfU, 1).front();
            std::get<5>(escape) = kHex.substr(unicodeControl & 0xfU, 1).front();
            part = {escape.data(), escape.size()};
        }
        else if (unit == 0 || byte < 0x20 || byte == 0x7f)
        {
            std::get<2>(escape) = kHex.substr(byte >> 4U, 1).front();
            std::get<3>(escape) = kHex.substr(byte & 0x0fU, 1).front();
            part = {escape.data(), 4};
            if (byte == '\n' || byte == '\r' || byte == '\t')
            {
                char escaped = 't';
                if (byte == '\n')
                {
                    escaped = 'n';
                }
                else if (byte == '\r')
                {
                    escaped = 'r';
                }
                std::get<1>(escape) = escaped;
                part = {escape.data(), 2};
            }
            consumed = 1;
        }
        else
        {
            part = text.substr(0, unit);
        }
        if (part.size() > destination.size() - used)
        {
            truncated = true;
            break;
        }
        std::ranges::copy(part, destination.subspan(used).begin());
        used += part.size();
        text.remove_prefix(consumed);
    }
    return used;
}

bool IsSupportedLogFormat(std::string_view format, std::size_t arguments) noexcept
{
    if (format.size() > kMaxLogFormatBytes || arguments > kMaxLogArguments)
    {
        return false;
    }
    std::size_t count{};
    while (!format.empty())
    {
        const char value = format.front();
        format.remove_prefix(1);
        if (value != '{' && value != '}')
        {
            continue;
        }
        if (format.empty())
        {
            return false;
        }
        if (value == '{' && format.front() == '}')
        {
            ++count;
        }
        else if (value != format.front())
        {
            return false;
        }
        format.remove_prefix(1);
    }
    return count == arguments;
}

std::string_view BoundLogText(std::string_view text, bool& truncated) noexcept
{
    if (text.size() <= kLogMessageCapacity)
    {
        return text;
    }
    truncated = true;
    return text.substr(0, CompletePrefix(text, kLogMessageCapacity));
}

LogIdentityText NormalizeLogArgument(const module::ModuleId& value, bool& /*truncated*/) noexcept
{
    LogIdentityText text;
    const auto digits = module::FormatModuleId(value);
    std::ranges::copy(digits, text.Bytes.begin());
    text.Used = digits.size();
    return text;
}

LogIdentityText NormalizeLogArgument(Error value, bool& /*truncated*/) noexcept
{
    LogIdentityText text;
    LogFormatBuffer buffer{.Bytes = text.Bytes};
    fmt::format_to(LogOutputIterator{buffer}, "{}:{}", value.Domain().Value(), value.Code());
    if (const auto native = value.Native())
    {
        fmt::format_to(LogOutputIterator{buffer}, " native={}:{}", native->Source.Value, native->Value);
    }
    text.Used = buffer.Used;
    return text;
}

void FinishLogMessage(LogRecord& record, const LogFormatBuffer& buffer) noexcept
{
    auto text = buffer.Text();
    if (buffer.Truncated)
    {
        // 输出上限可能恰落在 UTF-8 字符中间；保留最后一个完整前缀，不转义半个字符。
        const auto prefix = CompletePrefix(text, text.size());
        text = text.substr(0, prefix);
    }
    record.Truncated = record.Truncated || buffer.Truncated;
    record.MessageText.Assign(text, record.Truncated);
}

void RejectLogFormat(LogRecord& record) noexcept
{
    record.ConstructionFailed = true;
    record.MessageText.Assign("[log-format-rejected]", record.Truncated);
}

void SetRecordContext(LogRecord& record, Level level, std::string_view category, std::source_location location,
                      std::optional<Error> failure, LogTags tags) noexcept
{
    record = {};
    record.Severity = level;
    record.Failure = failure;
    record.Tags = tags;
    record.SourceLine = location.line();
    record.SourceColumn = location.column();
    record.CategoryText.Assign(BoundLogText(category, record.Truncated), record.Truncated);
    record.FileText.Assign(BoundedSource(location.file_name(), record.Truncated), record.Truncated);
    record.FunctionText.Assign(BoundedSource(location.function_name(), record.Truncated), record.Truncated);
}

void EncodeRecord(const LogRecord& record, EncodedLogLine& line) noexcept
{
    line = {};
    LogFormatBuffer buffer{.Bytes = line.Bytes};
    const auto out = LogOutputIterator{buffer};
    fmt::format_to(out, "seq={} level={} line={} column={} truncated={} construction_failed={}", record.Sequence,
                   static_cast<unsigned int>(record.Severity), record.SourceLine, record.SourceColumn, record.Truncated,
                   record.ConstructionFailed);
    if (record.Failure)
    {
        bool unused{};
        const auto error = NormalizeLogArgument(*record.Failure, unused);
        fmt::format_to(out, " error={}", LogFmtValue(error));
    }
    if (record.Tags.Module)
    {
        const auto digits = module::FormatModuleId(*record.Tags.Module);
        fmt::format_to(out, " module={}", std::string_view{digits.data(), digits.size()});
    }
    if (record.Tags.Scope)
    {
        const auto scope = *record.Tags.Scope;
        fmt::format_to(out, " scope={}:{}:{}", scope.Domain, scope.Slot, scope.Generation);
    }
    if (record.Tags.Declaration)
    {
        const auto digits = module::FormatModuleId(module::ModuleId{*record.Tags.Declaration});
        fmt::format_to(out, " declaration={}", std::string_view{digits.data(), digits.size()});
    }
    if (record.Tags.Operation)
    {
        fmt::format_to(out, " operation={}", *record.Tags.Operation);
    }
    fmt::format_to(out, " category={} file={} function={} message={}\n", record.Category(), record.File(),
                   record.Function(), record.Message());
    line.Size = buffer.Used;
    line.Truncated = buffer.Truncated;
}
} // namespace pg::log::detail
