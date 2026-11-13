#pragma once

#include <LogRecord.h>
#include <PanguLogExport.h>

#include <type_traits>

namespace pg::log
{
class LogWriter;
namespace detail
{
struct WriterControl;
class WriterControlOwner;
template <typename Builder>
    requires std::is_nothrow_invocable_r_v<void, const Builder&, LogRecord&>
SubmitResult SubmitLazy(const LogWriter& writer, Level level, const Builder& builder) noexcept;
} // namespace detail

// 可跨实例关闭保留；不保护业务对象或 Plugin 代码。复制/析构与提交不可同时改写同一句柄对象。
class PG_LOG_API LogWriter final
{
public:
    LogWriter() noexcept = default;
    ~LogWriter();
    LogWriter(const LogWriter& other) noexcept;
    LogWriter& operator=(const LogWriter& other) noexcept;
    LogWriter(LogWriter&& other) noexcept;
    LogWriter& operator=(LogWriter&& other) noexcept;

    [[nodiscard]] LogWriter WithTags(LogTags tags) const noexcept;

private:
    friend class detail::WriterControlOwner;
    template <typename Builder>
        requires std::is_nothrow_invocable_r_v<void, const Builder&, LogRecord&>
    friend SubmitResult detail::SubmitLazy(const LogWriter& writer, Level level, const Builder& builder) noexcept;
    explicit LogWriter(detail::WriterControl* control, LogTags tags) noexcept;
    SubmitResult SubmitBorrowed(Level level, const void* context,
                                void (*build)(const void*, LogRecord&) noexcept) const noexcept;
    detail::WriterControl* Control{};
    LogTags Tags;
};

namespace detail
{
// 仅前端内部接缝；builder 只同步借用，接纳和过滤成功后才调用，不保留其代码或析构器。
template <typename Builder>
    requires std::is_nothrow_invocable_r_v<void, const Builder&, LogRecord&>
SubmitResult SubmitLazy(const LogWriter& writer, Level level, const Builder& builder) noexcept
{
    // LogWriter 的移动明确清空源句柄；提交移动后的源是合法的 kDisabled 路径。
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    return writer.SubmitBorrowed(level, &builder, [](const void* context, LogRecord& record) noexcept
                                 { (*static_cast<const Builder*>(context))(record); });
}
} // namespace detail
} // namespace pg::log
