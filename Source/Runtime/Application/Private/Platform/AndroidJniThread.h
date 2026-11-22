#pragma once

#include <Error.h>
#include <RendererTypes.h>
#include <Result.h>
#include <jni.h>

#include <expected>

namespace pg::app::detail
{
// 返回 true 表示由调用方在退出时分离本次附加的线程。
[[nodiscard]] inline Result<bool> AttachActivityThread(JavaVM& vm) noexcept
{
    void* currentEnv{};
    auto status = vm.GetEnv(&currentEnv, JNI_VERSION_1_6);
    const bool detachOnExit = status == JNI_EDETACHED;
    if (detachOnExit)
    {
        JNIEnv* attachedEnv{};
        status = vm.AttachCurrentThread(&attachedEnv, nullptr);
    }
    if (status != JNI_OK)
    {
        constexpr NativeErrorSourceId kJni{0x6a6e69ULL};
        return std::unexpected(renderer::MakeRendererError(renderer::RendererStage::kOpen,
                                                           renderer::RendererError::kNativeFailure,
                                                           NativeErrorCode{.Source = kJni, .Value = status}));
    }
    return detachOnExit;
}
} // namespace pg::app::detail
