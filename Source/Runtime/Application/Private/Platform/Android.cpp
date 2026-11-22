#include "../GraphicsApplication.h"
#include "AndroidJniThread.h"

#include <Error.h>
#include <NativeSurface.h>
#include <RendererSession.h>
#include <RendererTypes.h>
#include <Result.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <jni.h>

#include <cstdint>
#include <expected>
#include <format>
#include <optional>

namespace pg::app::detail
{
namespace
{
constexpr const char* kLogTag = "PanguApplication";

class AndroidApplication final : public DesktopWindow
{
public:
    explicit AndroidApplication(android_app& app) noexcept
        : App(&app)
    {
    }

    [[nodiscard]] Status RunActivity() noexcept
    {
        App->userData = this;
        App->onAppCmd = DispatchCommand;
        auto result = RunGraphicsApplication(*this);
        if (App->destroyRequested == 0)
        {
            // 图形会话已关闭，Glue 仍需处理暂停和窗口撤销，才能进入 Activity 销毁。
            WindowFailure.reset();
            ANativeActivity_finish(App->activity);
            const auto destroyed = WaitForDestroy();
            if (result)
            {
                result = destroyed;
            }
        }
        App->onAppCmd = nullptr;
        App->userData = nullptr;
        return result;
    }

    [[nodiscard]] Result<std::optional<Drawable>> Poll() noexcept override
    {
        while (App->destroyRequested == 0)
        {
            const auto polled = PollEvent(0);
            if (!polled)
            {
                return std::unexpected(polled.error());
            }
            if (!*polled)
            {
                break;
            }
        }
        if (App->destroyRequested != 0)
        {
            return std::nullopt;
        }

        renderer::PixelSize size{};
        auto* const window = WindowReady && Resumed ? App->window : nullptr;
        if (window != nullptr)
        {
            const auto width = ANativeWindow_getWidth(window);
            const auto height = ANativeWindow_getHeight(window);
            if (width > 0 && height > 0)
            {
                size = {.Width = static_cast<std::uint32_t>(width), .Height = static_cast<std::uint32_t>(height)};
            }
        }
        return Drawable{.Surface = renderer::NativeSurface{renderer::AndroidSurface{.Window = window}}, .Size = size};
    }

    [[nodiscard]] Status WaitForEvent() noexcept override
    {
        const auto polled = PollEvent(-1);
        return polled ? Status{} : std::unexpected(polled.error());
    }

    [[nodiscard]] Status Run(renderer::RendererSession& session) noexcept override
    {
#ifdef PG_RENDERER_VULKAN_PREVIEW
        auto* const vm = App->activity->vm;
        const auto detachOnExit = AttachActivityThread(*vm);
        if (!detachOnExit)
        {
            return std::unexpected(detachOnExit.error());
        }
        const auto frames = RunGraphicsSession(session, *this, {.Activity = App->activity});
        if (*detachOnExit)
        {
            vm->DetachCurrentThread();
        }
        return frames;
#else
        const auto opened = session.Open();
        const auto closed = session.Close();
        if (!closed)
        {
            return closed;
        }
        if (opened)
        {
            return std::unexpected(
                renderer::MakeRendererError(renderer::RendererStage::kOpen, renderer::RendererError::kInvalidState));
        }
        if (opened.error() != renderer::MakeRendererError(renderer::RendererStage::kOpen,
                                                          renderer::RendererError::kPresentationUnavailable))
        {
            return opened;
        }
        __android_log_write(ANDROID_LOG_INFO, kLogTag, "presentation-unavailable");
        return WaitForDestroy();
#endif
    }

private:
    [[nodiscard]] Status WaitForDestroy() noexcept
    {
        while (App->destroyRequested == 0)
        {
            if (const auto events = WaitForEvent(); !events)
            {
                return events;
            }
        }
        return {};
    }

    static void DispatchCommand(android_app* app, std::int32_t command) noexcept
    {
        static_cast<AndroidApplication*>(app->userData)->HandleCommand(command);
    }

    void HandleCommand(std::int32_t command) noexcept
    {
        switch (command)
        {
        case APP_CMD_INIT_WINDOW:
            if (App->window != nullptr && !WindowReady)
            {
                WindowReady = true;
                ++Generation;
                __android_log_write(ANDROID_LOG_INFO, kLogTag,
                                    std::format("window-ready generation={}", Generation).c_str());
            }
            break;
        case APP_CMD_TERM_WINDOW:
            RetireWindow();
            break;
        case APP_CMD_PAUSE:
            Resumed = false;
            __android_log_write(ANDROID_LOG_INFO, kLogTag, "activity-paused");
            break;
        case APP_CMD_RESUME:
            Resumed = true;
            __android_log_write(ANDROID_LOG_INFO, kLogTag, "activity-resumed");
            break;
        case APP_CMD_DESTROY:
            RetireWindow();
            __android_log_write(ANDROID_LOG_INFO, kLogTag, "activity-destroyed");
            break;
        default:
            break;
        }
    }

    void RetireWindow() noexcept
    {
        if (WindowReady)
        {
            // NDK Glue 在命令返回后确认窗口撤销。
            if (const auto retired = RetireDrawable(); !retired)
            {
                WindowFailure = retired.error();
            }
            WindowReady = false;
            __android_log_write(ANDROID_LOG_INFO, kLogTag,
                                std::format("window-retired generation={}", Generation).c_str());
        }
    }

    [[nodiscard]] Result<bool> PollEvent(int timeout) noexcept
    {
        if (WindowFailure)
        {
            return std::unexpected(*WindowFailure);
        }
        void* data{};
        const auto result = ALooper_pollOnce(timeout, nullptr, nullptr, &data);
        if (result == ALOOPER_POLL_ERROR)
        {
            return std::unexpected(Error{ErrorDomainId::Constant<0x70672e617070ULL>(), 1});
        }
        if (result >= 0 && data != nullptr)
        {
            auto* const source = static_cast<android_poll_source*>(data);
            source->process(App, source);
        }
        if (WindowFailure)
        {
            return std::unexpected(*WindowFailure);
        }
        return result >= 0;
    }

    android_app* App;
    unsigned Generation{};
    bool WindowReady{};
    bool Resumed{};
    std::optional<Error> WindowFailure;
};
} // namespace
} // namespace pg::app::detail

extern "C" void android_main(android_app* app)
{
    pg::app::detail::AndroidApplication platform(*app);
    const auto result = platform.RunActivity();
    if (result)
    {
        __android_log_write(ANDROID_LOG_INFO, pg::app::detail::kLogTag, "host-closed");
    }
    else
    {
        auto message = std::format("host-failed code={}", result.error().Code());
        if (const auto native = result.error().Native())
        {
            message += std::format(" native-source={:x} native-code={}", native->Source.Value, native->Value);
        }
        __android_log_write(ANDROID_LOG_ERROR, pg::app::detail::kLogTag, message.c_str());
    }
}
