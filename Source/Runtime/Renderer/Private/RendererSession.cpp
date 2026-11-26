#include "FrameBackend.h"
#include "NativeSurface.h"

#include <Check.h>
#include <Error.h>
#include <RendererSession.h>
#include <RendererTypes.h>
#include <Result.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace pg::renderer
{
namespace
{
[[nodiscard]] std::atomic_flag& ProcessReservation() noexcept
{
    static constinit std::atomic_flag reserved;
    return reserved;
}

[[nodiscard]] std::unexpected<Error> Reject(RendererStage stage, RendererError cause) noexcept
{
    return std::unexpected(MakeRendererError(stage, cause));
}

[[nodiscard]] bool IsFinite(const TriangleVertex& vertex) noexcept
{
    const auto& color = vertex.Color;
    return std::isfinite(vertex.X) && std::isfinite(vertex.Y) && std::isfinite(color.R) && std::isfinite(color.G) &&
           std::isfinite(color.B) && std::isfinite(color.A);
}
} // namespace

RendererSession::RendererSession(std::unique_ptr<detail::FrameBackend> backend) noexcept
    : Backend(std::move(backend))
{
    PG_CHECK(Backend != nullptr, "RendererSession requires a backend");
    Backend->Session = this;
}

RendererSession::~RendererSession() noexcept
{
    if (State == SessionState::kClosed)
    {
        return;
    }
    PG_CHECK(OwnerThread() == std::this_thread::get_id(), "RendererSession must be destroyed on its session thread");
    [[maybe_unused]] const auto closed = Close();
}

Status RendererSession::Open() noexcept { return Open({}); }

Status RendererSession::Open(RendererOpenDesc desc) noexcept
{
    if (ProcessReservation().test_and_set(std::memory_order_acquire))
    {
        return Reject(RendererStage::kOpen, RendererError::kBusy);
    }
    SetOwner(std::this_thread::get_id());
    State = SessionState::kOpen;
    HasIndexedMesh = desc.Quad != nullptr;
    if (desc.Quad != nullptr &&
        (!std::ranges::all_of(desc.Quad->Vertices, IsFinite) ||
         !std::ranges::all_of(desc.Quad->Indices, [&](auto index) { return index < desc.Quad->Vertices.size(); })))
    {
        FirstCloseError = MakeRendererError(RendererStage::kOpen, RendererError::kInvalidVertex);
        return Close();
    }
    const auto opened = Backend->Open(desc);
    if (!opened)
    {
        FirstCloseError = opened.error();
        return Close();
    }
    return opened;
}

Status RendererSession::Attach(SurfaceId id, NativeSurface surface, PixelSize size) noexcept
{
    if (const auto ready = CanRender(RendererStage::kAttach); !ready)
    {
        return ready;
    }
    if (!id.IsValid())
    {
        return Reject(RendererStage::kAttach, RendererError::kInvalidSurface);
    }
    if (Attachment != AttachmentState::kNone)
    {
        return Reject(RendererStage::kAttach, RendererError::kBusy);
    }

    SetActiveSurface(id);
    if (size.Width == 0 || size.Height == 0)
    {
        Attachment = AttachmentState::kZeroSize;
        return {};
    }

    Attachment = AttachmentState::kAttached;
    const auto attached = Backend->Attach(surface, size);
    if (!attached)
    {
        RetireAttachment();
    }
    else if (IsDetachRequested())
    {
        RetireAttachment();
        return Reject(RendererStage::kAttach, RendererError::kInvalidSurface);
    }
    return attached;
}

Status RendererSession::Detach(SurfaceId id) noexcept
{
    const auto owner = OwnerThread();
    if (owner == std::thread::id{})
    {
        return {};
    }
    if (owner != std::this_thread::get_id())
    {
        return Reject(RendererStage::kDrain, RendererError::kWrongThread);
    }
    if (id == ActiveSurface)
    {
        RetireAttachment();
    }
    return {};
}

void RendererSession::RequestDetach(SurfaceId id) noexcept
{
    const std::scoped_lock lock(PublicationMutex);
    if (!id.IsValid() || id != ActiveSurface)
    {
        return;
    }
    if (Submission == SubmissionState::kIdle)
    {
        Submission = SubmissionState::kRevoked;
    }
    else if (Submission == SubmissionState::kAdmitted)
    {
        Submission = SubmissionState::kAdmittedThenRevoked;
    }
}

Status RendererSession::SetPaused(bool paused) noexcept
{
    if (const auto ready = CanRender(RendererStage::kAcquire); !ready)
    {
        return ready;
    }
    Paused = paused;
    if (IsDetachRequested())
    {
        RetireAttachment();
    }
    return {};
}

Result<FrameResult> RendererSession::Present(LinearColor color) noexcept { return PresentFrame(color, {}); }

Result<FrameResult> RendererSession::PresentIndexedMesh(LinearColor clearColor) noexcept
{
    return PresentFrame(clearColor, {.Indexed = true});
}

Result<FrameResult> RendererSession::PresentTriangle(LinearColor clearColor,
                                                     const std::array<TriangleVertex, 3>& vertices) noexcept
{
    return PresentFrame(clearColor, {.Triangle = &vertices});
}

Result<FrameResult> RendererSession::PresentFrame(LinearColor color, detail::FrameDraw draw) noexcept
{
    if (const auto ready = CanRender(RendererStage::kAcquire); !ready)
    {
        return std::unexpected(ready.error());
    }
    if (IsDetachRequested())
    {
        return RetireForReattach();
    }
    if (Paused || Attachment != AttachmentState::kAttached)
    {
        return FrameResult::kSkipped;
    }
    if (draw.Indexed && !HasIndexedMesh)
    {
        return Reject(RendererStage::kSubmit, RendererError::kInvalidState);
    }
    if (draw.Triangle != nullptr || draw.Indexed)
    {
        if (draw.Triangle != nullptr && !std::ranges::all_of(*draw.Triangle, IsFinite))
        {
            return Reject(RendererStage::kSubmit, RendererError::kInvalidVertex);
        }
        if (const auto prepared = Backend->PrepareTriangle(draw.Indexed); !prepared)
        {
            return FailFrame(prepared.error());
        }
    }

    const auto acquired = Backend->Acquire();
    if (!acquired)
    {
        return FailFrame(acquired.error());
    }
    if (*acquired == detail::AcquireResult::kSkipped)
    {
        return IsDetachRequested() ? RetireForReattach() : FrameResult::kSkipped;
    }
    if (*acquired == detail::AcquireResult::kReattach || !TryAdmitSubmission())
    {
        return RetireForReattach();
    }

    const auto submitted = Backend->Submit(color, draw);
    FinishSubmission();
    if (!submitted)
    {
        return FailFrame(submitted.error());
    }
    if (IsDetachRequested())
    {
        return RetireForReattach();
    }
    Backend->PresentImage();
    // 已返回的呈现调用计一次。新的撤销请求由下一次会话操作处理。
    return FrameResult::kPresentRequested;
}

Status RendererSession::Close() noexcept
{
    const auto owner = OwnerThread();
    if (owner == std::thread::id{})
    {
        return {};
    }
    if (owner != std::this_thread::get_id())
    {
        return Reject(RendererStage::kDrain, RendererError::kWrongThread);
    }

    State = SessionState::kClosing;
    RetireAttachment();
    Backend->Close();

    const auto error = std::exchange(FirstCloseError, std::nullopt);
    Paused = false;
    State = SessionState::kClosed;
    SetOwner({});
    ProcessReservation().clear(std::memory_order_release);
    return error ? Status{std::unexpected(*error)} : Status{};
}

std::thread::id RendererSession::OwnerThread() const noexcept
{
    const std::scoped_lock lock(PublicationMutex);
    return Owner;
}

void RendererSession::SetOwner(std::thread::id owner) noexcept
{
    const std::scoped_lock lock(PublicationMutex);
    Owner = owner;
}

bool RendererSession::IsDetachRequested() const noexcept
{
    const std::scoped_lock lock(PublicationMutex);
    return Submission == SubmissionState::kRevoked || Submission == SubmissionState::kAdmittedThenRevoked;
}

bool RendererSession::TryAdmitSubmission() noexcept
{
    const std::scoped_lock lock(PublicationMutex);
    if (Submission != SubmissionState::kIdle)
    {
        return false;
    }
    Submission = SubmissionState::kAdmitted;
    return true;
}

void RendererSession::FinishSubmission() noexcept
{
    const std::scoped_lock lock(PublicationMutex);
    Submission =
        Submission == SubmissionState::kAdmittedThenRevoked ? SubmissionState::kRevoked : SubmissionState::kIdle;
}

Status RendererSession::CanRender(RendererStage stage) const noexcept
{
    const auto owner = OwnerThread();
    if (owner == std::thread::id{})
    {
        return Reject(stage, RendererError::kInvalidState);
    }
    if (owner != std::this_thread::get_id())
    {
        return Reject(stage, RendererError::kWrongThread);
    }
    if (State == SessionState::kClosing)
    {
        return FirstCloseError ? std::unexpected(*FirstCloseError) : Reject(stage, RendererError::kInvalidState);
    }
    return {};
}

void RendererSession::RetireAttachment() noexcept
{
    if (Attachment == AttachmentState::kNone)
    {
        return;
    }
    if (Attachment == AttachmentState::kAttached)
    {
        Backend->RetireSurface();
    }
    Attachment = AttachmentState::kNone;
    SetActiveSurface(SurfaceId{});
}

FrameResult RendererSession::RetireForReattach() noexcept
{
    RetireAttachment();
    return FrameResult::kReattach;
}

Result<FrameResult> RendererSession::FailFrame(Error error) noexcept
{
    State = SessionState::kClosing;
    FirstCloseError = error;
    return std::unexpected(error);
}

void RendererSession::SetActiveSurface(SurfaceId id) noexcept
{
    const std::scoped_lock lock(PublicationMutex);
    ActiveSurface = id;
    Submission = SubmissionState::kIdle;
}

bool detail::FrameBackend::IsDetachRequested() const noexcept { return Session->IsDetachRequested(); }

SurfaceId detail::FrameBackend::CurrentSurfaceId() const noexcept { return Session->ActiveSurface; }
} // namespace pg::renderer
