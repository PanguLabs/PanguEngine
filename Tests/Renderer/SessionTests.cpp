#include "FakeFrameBackend.h"
#include "FrameBackend.h"
#include "NativeSurface.h"

#include <Error.h>
#include <RendererSession.h>
#include <RendererTypes.h>
#include <Result.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <thread>

namespace pg::renderer::detail
{
namespace
{
constexpr SurfaceId kIdA{17};
constexpr SurfaceId kIdB{18};
constexpr PixelSize kSize{.Width = 640, .Height = 480};
constexpr LinearColor kColor{.R = 0.05F, .G = 0.32F, .B = 0.58F, .A = 1.0F};
constexpr NativeErrorCode kNativeFault{.Source = NativeErrorSourceId{72}, .Value = -106};

// 两次挂接使用同一原生句柄值；假后端不解引用它。
const NativeSurface kWindow{XlibSurface{.Display = nullptr, .Window = 123}};

class SessionRig final
{
public:
    [[nodiscard]] FakeFrameBackend& Backend() const noexcept { return *Observer; }
    [[nodiscard]] RendererSession& Session() noexcept { return SessionValue; }

private:
    FakeFrameBackend* Observer{};
    RendererSession SessionValue = RendererSessionTestAccess::Make(Observer);
};

template <typename T>
void ExpectError(const Result<T>& result, RendererStage stage, RendererError cause,
                 std::optional<NativeErrorCode> native = std::nullopt)
{
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().Domain(), kRendererErrorDomain);
    EXPECT_EQ(GetRendererStage(result.error()), stage);
    EXPECT_EQ(GetRendererCause(result.error()), cause);
    EXPECT_EQ(result.error().Native(), native);
}

[[nodiscard]] auto Calls(const FakeFrameBackend& backend, BackendCall call)
{
    return std::ranges::count(backend.Observations().Events, call, &BackendEvent::Call);
}

void ExpectBusyOpen(RendererSession& session)
{
    const auto opened = session.Open();
    ExpectError(opened, RendererStage::kOpen, RendererError::kBusy);
    if (opened)
    {
        EXPECT_TRUE(session.Close());
    }
}

TEST(Session, BusyAndRollback)
{
    BackendGate opening;
    BackendGate rollback;
    SessionRig first;
    SessionRig second;
    first.Backend().Controls().OpenGate = &opening;
    first.Backend().Controls().CloseGate = &rollback;
    first.Backend().FailNext(RendererStage::kOpen, RendererError::kNoDevice, kNativeFault);
    Status firstResult;
    std::jthread owner([&] { firstResult = first.Session().Open(); });

    EXPECT_TRUE(opening.Entered.try_acquire_for(BackendGate::kDeadline));
    ExpectBusyOpen(second.Session());
    opening.Resume.release();
    EXPECT_TRUE(rollback.Entered.try_acquire_for(BackendGate::kDeadline));
    ExpectBusyOpen(second.Session());
    rollback.Resume.release();
    owner.join();

    ExpectError(firstResult, RendererStage::kOpen, RendererError::kNoDevice, kNativeFault);
    EXPECT_EQ(Calls(first.Backend(), BackendCall::kClose), 1);
    EXPECT_EQ(Calls(second.Backend(), BackendCall::kOpen), 0);
    ASSERT_TRUE(second.Session().Open());
    ExpectBusyOpen(first.Session());
    EXPECT_TRUE(second.Session().Close());
    EXPECT_TRUE(second.Session().Close());
    EXPECT_EQ(Calls(second.Backend(), BackendCall::kClose), 1);
}

TEST(Session, StaleDetach)
{
    SessionRig rig;
    auto& session = rig.Session();
    const auto& backend = rig.Backend();
    ASSERT_TRUE(session.Open());
    ASSERT_TRUE(session.Attach(kIdA, kWindow, kSize));
    ExpectError(session.Attach(kIdB, kWindow, kSize), RendererStage::kAttach, RendererError::kBusy);
    ASSERT_TRUE(session.Detach(kIdA));
    ASSERT_TRUE(session.Attach(kIdB, kWindow, kSize));
    session.RequestDetach(kIdA);
    ASSERT_TRUE(session.Detach(kIdA));

    ASSERT_EQ(session.Present(kColor), FrameResult::kPresentRequested);
    ASSERT_FALSE(backend.Observations().Events.empty());
    const auto& presented = backend.Observations().Events.back();
    EXPECT_EQ(presented.Call, BackendCall::kPresent);
    EXPECT_EQ(presented.Surface, kIdB);
    EXPECT_EQ(Calls(backend, BackendCall::kRetireSurface), 1);
    EXPECT_EQ(backend.Observations().LastColor.B, kColor.B);

    session.RequestDetach(kIdB);
    session.RequestDetach(kIdA);
    EXPECT_EQ(session.Present(kColor), FrameResult::kReattach);
    EXPECT_EQ(Calls(backend, BackendCall::kRetireSurface), 2);
    EXPECT_TRUE(session.Close());
}

TEST(Session, ZeroSizeAndPausePreserveDeviceWithoutAcquiring)
{
    SessionRig rig;
    auto& session = rig.Session();
    const auto& backend = rig.Backend();
    ASSERT_TRUE(session.Open());
    ASSERT_TRUE(session.Attach(kIdA, kWindow, {.Width = kSize.Width, .Height = 0}));
    EXPECT_EQ(session.Present(kColor), FrameResult::kSkipped);
    EXPECT_EQ(Calls(backend, BackendCall::kAttach), 0);
    ASSERT_TRUE(session.Detach(kIdA));

    ASSERT_TRUE(session.Attach(kIdB, kWindow, kSize));
    ASSERT_TRUE(session.SetPaused(true));
    EXPECT_EQ(session.Present(kColor), FrameResult::kSkipped);
    EXPECT_EQ(Calls(backend, BackendCall::kAcquire), 0);
    EXPECT_EQ(backend.Observations().SuccessfulSubmissions, 0U);
    ASSERT_TRUE(session.SetPaused(false));
    ASSERT_EQ(session.Present(kColor), FrameResult::kPresentRequested);
    ASSERT_FALSE(backend.Observations().Events.empty());
    const auto& sample = backend.Observations().Events.back();
    EXPECT_EQ(sample.Call, BackendCall::kPresent);
    EXPECT_EQ(sample.Surface, kIdB);
    EXPECT_EQ(sample.Device, 1U);
    EXPECT_EQ(sample.SubmittedFrame, 1U);
    EXPECT_TRUE(session.Close());
}

TEST(Session, AttachFailureRollsBackSurfaceWithoutClosingDevice)
{
    SessionRig rig;
    auto& session = rig.Session();
    auto& backend = rig.Backend();
    ASSERT_TRUE(session.Open());
    backend.FailNext(RendererStage::kAttach, RendererError::kNativeFailure, kNativeFault);
    ExpectError(session.Attach(kIdA, kWindow, kSize), RendererStage::kAttach, RendererError::kNativeFailure,
                kNativeFault);
    EXPECT_EQ(Calls(backend, BackendCall::kRetireSurface), 1);

    ASSERT_TRUE(session.Attach(kIdB, kWindow, kSize));
    EXPECT_EQ(session.Present(kColor), FrameResult::kPresentRequested);
    EXPECT_EQ(Calls(backend, BackendCall::kOpen), 1);
    EXPECT_EQ(Calls(backend, BackendCall::kClose), 0);
    EXPECT_TRUE(session.Close());
}

TEST(Session, RevocationDuringAttachReportsAnUnusableSurface)
{
    SessionRig rig;
    auto& session = rig.Session();
    auto& backend = rig.Backend();
    BackendGate attach;
    backend.Controls().AttachGate = &attach;
    ASSERT_TRUE(session.Open());
    bool entered{};
    std::jthread notifier(
        [&]
        {
            entered = attach.Entered.try_acquire_for(BackendGate::kDeadline);
            session.RequestDetach(kIdA);
            attach.Resume.release();
        });

    const auto attached = session.Attach(kIdA, kWindow, kSize);
    notifier.join();
    EXPECT_TRUE(entered);
    ExpectError(attached, RendererStage::kAttach, RendererError::kInvalidSurface);
    EXPECT_EQ(Calls(backend, BackendCall::kRetireSurface), 1);
    EXPECT_TRUE(session.Detach(kIdA));
    EXPECT_TRUE(session.Close());
}

void ExpectAcquireRevocation(AcquireResult acquisition)
{
    SessionRig rig;
    auto& session = rig.Session();
    auto& backend = rig.Backend();
    ASSERT_TRUE(session.Open());
    ASSERT_TRUE(session.Attach(kIdA, kWindow, kSize));
    BackendGate acquire;
    backend.Controls().AcquireGate = &acquire;
    backend.Controls().NextAcquire = acquisition;
    bool entered{};
    bool admittedBeforeRevocation{};
    std::jthread notifier(
        [&]
        {
            entered = acquire.Entered.try_acquire_for(BackendGate::kDeadline);
            admittedBeforeRevocation = RendererSessionTestAccess::HasAdmittedSubmission(session);
            session.RequestDetach(kIdA);
            acquire.Resume.release();
        });

    const auto result = session.Present(kColor);
    notifier.join();
    EXPECT_TRUE(entered);
    EXPECT_FALSE(admittedBeforeRevocation);
    EXPECT_TRUE(backend.Observations().SawRevocationDuringAcquire);
    EXPECT_EQ(result, FrameResult::kReattach);
    EXPECT_EQ(Calls(backend, BackendCall::kAcquire), 1);
    EXPECT_EQ(Calls(backend, BackendCall::kSubmit), 0);
    EXPECT_EQ(Calls(backend, BackendCall::kPresent), 0);
    EXPECT_EQ(Calls(backend, BackendCall::kRetireSurface), 1);
    EXPECT_TRUE(session.Close());
    EXPECT_TRUE(std::ranges::all_of(backend.Observations().Events,
                                    [owner = std::this_thread::get_id()](const BackendEvent& event)
                                    { return event.Thread == owner; }));
}

TEST(Session, RevocationWithSkippedAcquireRetiresSurface) { ExpectAcquireRevocation(AcquireResult::kSkipped); }

TEST(Session, RevocationAfterAcquireRetiresSurfaceWithoutSubmitting) { ExpectAcquireRevocation(AcquireResult::kReady); }

TEST(Session, RevocationAfterSubmitAdmissionRetiresTheInFlightFrame)
{
    SessionRig rig;
    auto& session = rig.Session();
    auto& backend = rig.Backend();
    ASSERT_TRUE(session.Open());
    ASSERT_TRUE(session.Attach(kIdA, kWindow, kSize));
    BackendGate submit;
    backend.Controls().SubmitGate = &submit;
    bool entered{};
    bool admittedBeforeRevocation{};
    bool admittedAfterRevocation{};
    std::jthread notifier(
        [&]
        {
            entered = submit.Entered.try_acquire_for(BackendGate::kDeadline);
            admittedBeforeRevocation = RendererSessionTestAccess::HasAdmittedSubmission(session);
            session.RequestDetach(kIdA);
            admittedAfterRevocation = RendererSessionTestAccess::HasAdmittedSubmission(session);
            submit.Resume.release();
        });

    const auto result = session.Present(kColor);
    notifier.join();
    EXPECT_TRUE(entered);
    EXPECT_TRUE(admittedBeforeRevocation);
    EXPECT_TRUE(admittedAfterRevocation);
    EXPECT_TRUE(submit.Resumed);
    EXPECT_EQ(result, FrameResult::kReattach);
    EXPECT_FALSE(RendererSessionTestAccess::HasAdmittedSubmission(session));
    EXPECT_EQ(Calls(backend, BackendCall::kAcquire), 1);
    EXPECT_EQ(Calls(backend, BackendCall::kSubmit), 1);
    EXPECT_EQ(Calls(backend, BackendCall::kPresent), 0);
    EXPECT_EQ(backend.Observations().SuccessfulSubmissions, 1U);
    EXPECT_EQ(Calls(backend, BackendCall::kRetireSurface), 1);
    ASSERT_FALSE(backend.Observations().Events.empty());
    const auto& retired = backend.Observations().Events.back();
    EXPECT_EQ(retired.Call, BackendCall::kRetireSurface);
    EXPECT_EQ(retired.SubmittedFrame, 1U);
    EXPECT_TRUE(session.Close());
}

TEST(Session, AcquireFailureDoesNotSubmit)
{
    SessionRig rig;
    auto& session = rig.Session();
    auto& backend = rig.Backend();
    ASSERT_TRUE(session.Open());
    ASSERT_TRUE(session.Attach(kIdA, kWindow, kSize));
    backend.FailNext(RendererStage::kAcquire, RendererError::kNativeFailure, kNativeFault);

    ExpectError(session.Present(kColor), RendererStage::kAcquire, RendererError::kNativeFailure, kNativeFault);
    EXPECT_EQ(Calls(backend, BackendCall::kSubmit), 0);
    ExpectError(session.Close(), RendererStage::kAcquire, RendererError::kNativeFailure, kNativeFault);
    EXPECT_EQ(Calls(backend, BackendCall::kRetireSurface), 1);
    EXPECT_TRUE(session.Close());
}

TEST(Session, AcquireSkipAndReattachDoNotSubmit)
{
    SessionRig rig;
    auto& session = rig.Session();
    auto& backend = rig.Backend();
    ASSERT_TRUE(session.Open());
    ASSERT_TRUE(session.Attach(kIdA, kWindow, kSize));
    backend.Controls().NextAcquire = AcquireResult::kSkipped;
    EXPECT_EQ(session.Present(kColor), FrameResult::kSkipped);
    EXPECT_EQ(Calls(backend, BackendCall::kRetireSurface), 0);

    backend.Controls().NextAcquire = AcquireResult::kReattach;
    EXPECT_EQ(session.Present(kColor), FrameResult::kReattach);
    EXPECT_EQ(backend.Observations().SuccessfulSubmissions, 0U);
    EXPECT_EQ(Calls(backend, BackendCall::kRetireSurface), 1);
    EXPECT_TRUE(session.Attach(kIdB, kWindow, kSize));
    EXPECT_TRUE(session.Close());
}

TEST(Session, SubmitFailure)
{
    SessionRig rig;
    auto& session = rig.Session();
    auto& backend = rig.Backend();
    ASSERT_TRUE(session.Open());
    ASSERT_TRUE(session.Attach(kIdA, kWindow, kSize));
    backend.FailNext(RendererStage::kSubmit, RendererError::kNativeFailure, kNativeFault);

    ExpectError(session.Present(kColor), RendererStage::kSubmit, RendererError::kNativeFailure, kNativeFault);
    EXPECT_EQ(Calls(backend, BackendCall::kSubmit), 1);
    EXPECT_EQ(backend.Observations().SuccessfulSubmissions, 0U);
    EXPECT_EQ(Calls(backend, BackendCall::kPresent), 0);
    ExpectError(session.Close(), RendererStage::kSubmit, RendererError::kNativeFailure, kNativeFault);
    EXPECT_EQ(Calls(backend, BackendCall::kRetireSurface), 1);
    EXPECT_TRUE(session.Close());
}

TEST(Session, RevocationDuringPresentationReportsTheReturnedCallOnce)
{
    SessionRig rig;
    auto& session = rig.Session();
    auto& backend = rig.Backend();
    BackendGate present;
    backend.Controls().PresentGate = &present;
    ASSERT_TRUE(session.Open());
    ASSERT_TRUE(session.Attach(kIdA, kWindow, kSize));
    bool entered{};
    std::jthread notifier(
        [&]
        {
            entered = present.Entered.try_acquire_for(BackendGate::kDeadline);
            session.RequestDetach(kIdA);
            present.Resume.release();
        });

    const auto frame = session.Present(kColor);
    notifier.join();
    EXPECT_TRUE(entered);
    EXPECT_EQ(frame, FrameResult::kPresentRequested);
    EXPECT_EQ(session.Present(kColor), FrameResult::kReattach);
    EXPECT_EQ(Calls(backend, BackendCall::kPresent), 1);
    EXPECT_EQ(Calls(backend, BackendCall::kRetireSurface), 1);
    EXPECT_TRUE(session.Close());
}

TEST(Session, WrongThreadCannotDetachOrClose)
{
    SessionRig rig;
    auto& session = rig.Session();
    ASSERT_TRUE(session.Open());
    ASSERT_TRUE(session.Attach(kIdA, kWindow, kSize));
    Status detach;
    Status close;
    std::jthread wrong(
        [&]
        {
            detach = session.Detach(kIdA);
            close = session.Close();
        });
    wrong.join();

    ExpectError(detach, RendererStage::kDrain, RendererError::kWrongThread);
    ExpectError(close, RendererStage::kDrain, RendererError::kWrongThread);
    EXPECT_EQ(Calls(rig.Backend(), BackendCall::kRetireSurface), 0);
    EXPECT_EQ(session.Present(kColor), FrameResult::kPresentRequested);
    EXPECT_TRUE(session.Close());
}
} // namespace
} // namespace pg::renderer::detail
