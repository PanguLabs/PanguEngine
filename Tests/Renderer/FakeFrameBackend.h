#pragma once

#include "FrameBackend.h"

#include <RendererSession.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <semaphore>
#include <thread>
#include <utility>
#include <vector>

namespace pg::renderer::detail
{
enum class BackendCall : std::uint8_t
{
    kOpen,
    kAttach,
    kPrepareTriangle,
    kAcquire,
    kSubmit,
    kPresent,
    kRetireSurface,
    kClose,
};

struct BackendEvent
{
    BackendCall Call;
    SurfaceId Surface;
    std::uint64_t Device;
    std::uint64_t SubmittedFrame;
    std::thread::id Thread;
};

struct BackendGate
{
    static constexpr auto kDeadline = std::chrono::seconds(3);
    std::binary_semaphore Entered{0};
    std::binary_semaphore Resume{0};
    bool Resumed{};

    void Pause() noexcept
    {
        Entered.release();
        Resumed = Resume.try_acquire_for(kDeadline);
    }
};

struct BackendControls
{
    BackendGate* OpenGate{};
    BackendGate* AttachGate{};
    BackendGate* CloseGate{};
    BackendGate* AcquireGate{};
    BackendGate* SubmitGate{};
    BackendGate* PresentGate{};
    AcquireResult NextAcquire = AcquireResult::kReady;
    std::optional<Error> NextTrianglePreparationError;
};

struct BackendObservations
{
    std::uint64_t IndexedSubmissions{};
    std::uint64_t QuadOpens{};
    std::uint64_t SuccessfulSubmissions{};
    LinearColor LastColor;
    std::optional<MeshTransform> LastTransform;
    std::optional<std::array<TriangleVertex, 3>> LastTriangle;
    bool SawRevocationDuringAcquire{};
    std::vector<BackendEvent> Events;
};

class FakeFrameBackend final : public FrameBackend
{
public:
    [[nodiscard]] BackendControls& Controls() noexcept { return Script; }
    [[nodiscard]] const BackendObservations& Observations() const noexcept { return History; }

    void FailNext(RendererStage stage, RendererError cause, NativeErrorCode native) noexcept
    {
        Failures.at(static_cast<std::size_t>(stage)) = MakeRendererError(stage, cause, native);
    }

    [[nodiscard]] Status Open(RendererOpenDesc desc) noexcept override
    {
        if (desc.Quad != nullptr)
        {
            ++History.QuadOpens;
        }
        ++DeviceSerial;
        Record(BackendCall::kOpen);
        if (Script.OpenGate != nullptr)
        {
            Script.OpenGate->Pause();
        }
        return TakeFailure(RendererStage::kOpen);
    }

    [[nodiscard]] Status Attach(NativeSurface /*surface*/, PixelSize /*size*/) noexcept override
    {
        Record(BackendCall::kAttach);
        if (Script.AttachGate != nullptr)
        {
            Script.AttachGate->Pause();
        }
        return TakeFailure(RendererStage::kAttach);
    }

    [[nodiscard]] Status PrepareGeometry(FrameDraw /*draw*/) noexcept override
    {
        Record(BackendCall::kPrepareTriangle);
        const auto error = std::exchange(Script.NextTrianglePreparationError, std::nullopt);
        return error ? Status{std::unexpected(*error)} : Status{};
    }

    [[nodiscard]] Result<AcquireResult> Acquire() noexcept override
    {
        Record(BackendCall::kAcquire);
        if (const auto status = TakeFailure(RendererStage::kAcquire); !status)
        {
            return std::unexpected(status.error());
        }
        if (Script.AcquireGate != nullptr)
        {
            Script.AcquireGate->Pause();
        }
        History.SawRevocationDuringAcquire = IsDetachRequested();
        return Script.NextAcquire;
    }

    [[nodiscard]] Status Submit(LinearColor color, FrameDraw draw) noexcept override
    {
        if (Script.SubmitGate != nullptr)
        {
            Script.SubmitGate->Pause();
        }
        Record(BackendCall::kSubmit);
        History.LastColor = color;
        History.LastTransform = draw.Transform != nullptr ? std::optional{*draw.Transform} : std::nullopt;
        History.LastTriangle = draw.Triangle != nullptr ? std::optional{*draw.Triangle} : std::nullopt;
        if (draw.Indexed)
        {
            ++History.IndexedSubmissions;
        }
        if (const auto status = TakeFailure(RendererStage::kSubmit); !status)
        {
            return status;
        }
        ++History.SuccessfulSubmissions;
        return {};
    }

    void PresentImage() noexcept override
    {
        Record(BackendCall::kPresent);
        if (Script.PresentGate != nullptr)
        {
            Script.PresentGate->Pause();
        }
    }

    void RetireSurface() noexcept override { Record(BackendCall::kRetireSurface); }

    void Close() noexcept override
    {
        Record(BackendCall::kClose);
        if (Script.CloseGate != nullptr)
        {
            Script.CloseGate->Pause();
        }
    }

private:
    [[nodiscard]] Status TakeFailure(RendererStage stage) noexcept
    {
        const auto error = std::exchange(Failures.at(static_cast<std::size_t>(stage)), std::nullopt);
        return error ? Status{std::unexpected(*error)} : Status{};
    }

    void Record(BackendCall call) noexcept
    {
        History.Events.push_back({
            .Call = call,
            .Surface = CurrentSurfaceId(),
            .Device = DeviceSerial,
            .SubmittedFrame = History.SuccessfulSubmissions,
            .Thread = std::this_thread::get_id(),
        });
    }

    BackendControls Script;
    BackendObservations History;
    std::array<std::optional<Error>, static_cast<std::size_t>(RendererStage::kSubmit) + 1> Failures;
    std::uint64_t DeviceSerial{};
};

class RendererSessionTestAccess final
{
public:
    [[nodiscard]] static RendererSession Make(std::unique_ptr<FrameBackend> backend) noexcept
    {
        return RendererSession{std::move(backend)};
    }

    [[nodiscard]] static RendererSession Make(FakeFrameBackend*& observer)
    {
        auto backend = std::make_unique<FakeFrameBackend>();
        observer = backend.get();
        return Make(std::move(backend));
    }

    [[nodiscard]] static bool HasAdmittedSubmission(const RendererSession& session) noexcept
    {
        const std::scoped_lock lock(session.PublicationMutex);
        return session.Submission == RendererSession::SubmissionState::kAdmitted ||
               session.Submission == RendererSession::SubmissionState::kAdmittedThenRevoked;
    }
};
} // namespace pg::renderer::detail
