#pragma once

#include <PanguRendererExport.h>
#include <RendererTypes.h>
#include <Result.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace pg::renderer
{
namespace detail
{
class FrameBackend;
class RendererSessionFactory;
class RendererSessionTestAccess;
} // namespace detail

class NativeSurface;

class PG_RENDERER_API RendererSession final
{
public:
    ~RendererSession() noexcept;
    RendererSession(const RendererSession&) = delete;
    RendererSession& operator=(const RendererSession&) = delete;
    RendererSession(RendererSession&&) = delete;
    RendererSession& operator=(RendererSession&&) = delete;

    // Open 绑定调用线程。除 RequestDetach 外，其余操作及未关闭会话的销毁均由该线程串行执行。
    [[nodiscard]] Status Open() noexcept;
    [[nodiscard]] Status Open(RendererOpenDesc desc) noexcept;
    // 由 Application 为每次尝试分配新代次，挂接成功后保活窗口至 Detach 或 Close。
    [[nodiscard]] Status Attach(SurfaceId id, NativeSurface surface, PixelSize size) noexcept;
    // 会话线程同步解除指定代次的窗口借用。
    [[nodiscard]] Status Detach(SurfaceId id) noexcept;
    void RequestDetach(SurfaceId id) noexcept;
    [[nodiscard]] Status SetPaused(bool paused) noexcept;
    [[nodiscard]] Result<FrameResult> Present(LinearColor color) noexcept;
    // 会话线程同步清理并释放进程占用，返回已记录的首个错误。
    [[nodiscard]] Status Close() noexcept;

private:
    friend class detail::FrameBackend;
    friend class detail::RendererSessionFactory;
    friend class detail::RendererSessionTestAccess;
    explicit RendererSession(std::unique_ptr<detail::FrameBackend> backend) noexcept;

    enum class SessionState : std::uint8_t
    {
        kClosed,
        kOpen,
        kClosing,
    };

    enum class AttachmentState : std::uint8_t
    {
        kNone,
        kZeroSize,
        kAttached,
    };

    enum class SubmissionState : std::uint8_t
    {
        kIdle,
        kAdmitted,
        kRevoked,
        kAdmittedThenRevoked,
    };

    [[nodiscard]] std::thread::id OwnerThread() const noexcept;
    void SetOwner(std::thread::id owner) noexcept;
    [[nodiscard]] bool IsDetachRequested() const noexcept;
    [[nodiscard]] bool TryAdmitSubmission() noexcept;
    void FinishSubmission() noexcept;
    [[nodiscard]] Status CanRender(RendererStage stage) const noexcept;
    void RetireAttachment() noexcept;
    [[nodiscard]] FrameResult RetireForReattach() noexcept;
    [[nodiscard]] Result<FrameResult> FailFrame(Error error) noexcept;
    void SetActiveSurface(SurfaceId id) noexcept;

    std::unique_ptr<detail::FrameBackend> Backend;
    std::optional<Error> FirstCloseError;
    SessionState State = SessionState::kClosed;
    AttachmentState Attachment = AttachmentState::kNone;
    bool Paused{};

    mutable std::mutex PublicationMutex;
    std::thread::id Owner;
    SurfaceId ActiveSurface;
    SubmissionState Submission = SubmissionState::kIdle;
};
} // namespace pg::renderer
