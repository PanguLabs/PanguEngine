#include "AndroidJniThread.h"

#include <Error.h>
#include <RendererTypes.h>
#include <jni.h>

#include <cstdlib>

namespace
{
struct CallState
{
    jint GetEnvStatus{};
    jint AttachStatus{};
    unsigned AttachCalls{};
};

bool Check(jint getEnvStatus, jint attachStatus, jint expectedStatus) noexcept
{
    JNIInvokeInterface calls{};
    CallState state{.GetEnvStatus = getEnvStatus, .AttachStatus = attachStatus};
    calls.reserved0 = &state;
    calls.GetEnv = [](JavaVM* vm, void**, jint)
    { return static_cast<CallState*>(vm->functions->reserved0)->GetEnvStatus; };
    calls.AttachCurrentThread = [](JavaVM* vm, JNIEnv**, void*)
    {
        auto* test = static_cast<CallState*>(vm->functions->reserved0);
        ++test->AttachCalls;
        return test->AttachStatus;
    };
    JavaVM vm{.functions = &calls};
    const bool needsAttach = getEnvStatus == JNI_EDETACHED;
    const auto result = pg::app::detail::AttachActivityThread(vm);
    if (state.AttachCalls != static_cast<unsigned>(needsAttach))
    {
        return false;
    }
    if (expectedStatus == JNI_OK)
    {
        return result && *result == needsAttach;
    }
    constexpr pg::NativeErrorSourceId kJni{0x6a6e69ULL};
    return !result && result.error() ==
                          pg::renderer::MakeRendererError(pg::renderer::RendererStage::kOpen,
                                                          pg::renderer::RendererError::kNativeFailure,
                                                          pg::NativeErrorCode{.Source = kJni, .Value = expectedStatus});
}
} // namespace

int main()
{
    return Check(JNI_EVERSION, JNI_OK, JNI_EVERSION) && Check(JNI_EDETACHED, JNI_ENOMEM, JNI_ENOMEM) &&
                   Check(JNI_OK, JNI_ERR, JNI_OK) && Check(JNI_EDETACHED, JNI_OK, JNI_OK)
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
