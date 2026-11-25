#include "../GraphicsApplication.h"

#include "NativeSurface.h"
#include "WaylandConfiguration.h"

#include <Error.h>
#include <Result.h>
#include <libdecor.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include <cstdint>
#include <cstdio> // IWYU pragma: keep
#include <expected>
#include <memory>
#include <optional>
#include <print>
#include <string_view>

namespace pg::app::detail
{
namespace
{
[[nodiscard]] std::unexpected<Error> WindowError() noexcept
{
    return std::unexpected(Error{ErrorDomainId::Constant<0x70672e617070ULL>(), 1});
}

template <auto Destroy>
struct WaylandDeleter
{
    void operator()(auto* object) const noexcept { Destroy(object); }
};

class WaylandWindow final : public DesktopWindow
{
public:
    [[nodiscard]] Status Run(renderer::RendererSession& session) noexcept override
    {
        if (const auto created = Create(); !created)
        {
            return created;
        }
        const auto native = NativeWindow();
        return RunGraphicsSession(session, *this, {.PresentationSurface = &native});
    }

    [[nodiscard]] Result<std::optional<Drawable>> Poll() noexcept override
    {
        if (!Connection)
        {
            if (const auto created = Create(); !created)
            {
                return std::unexpected(created.error());
            }
        }
        if (wl_display_get_error(Connection.get()) != 0 || libdecor_dispatch(Decorations.get(), 0) < 0 ||
            ConfigurationFailed)
        {
            return WindowError();
        }
        if (Closing)
        {
            return std::nullopt;
        }
        return Drawable{
            .Surface = NativeWindow(),
            .Size = Configuration.DrawableSize(),
        };
    }

    [[nodiscard]] Status WaitForEvent() noexcept override
    {
        // 默认队列可能已由 Vulkan WSI 读入。处理已有事件后立即回到窗口循环。
        return wl_display_dispatch(Connection.get()) < 0 ? Status{WindowError()} : Status{};
    }

private:
    [[nodiscard]] renderer::NativeSurface NativeWindow() const noexcept
    {
        return renderer::NativeSurface{renderer::WaylandSurface{.Display = Connection.get(), .Surface = Surface.get()}};
    }

    static void BindGlobal(void* data, wl_registry* registry, std::uint32_t name, const char* interface,
                           std::uint32_t /*version*/) noexcept
    {
        auto& window = *static_cast<WaylandWindow*>(data);
        const std::string_view announced{interface};
        if (!window.Compositor && announced == wl_compositor_interface.name)
        {
            window.Compositor.reset(
                static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, 1)));
        }
        else if (announced == "xdg_wm_base")
        {
            window.HasShell = true;
        }
    }

    static void Configure(libdecor_frame* frame, libdecor_configuration* configuration, void* data) noexcept
    {
        auto& window = *static_cast<WaylandWindow*>(data);
        int width{};
        int height{};
        std::optional<bool> suspended;
        libdecor_window_state state = LIBDECOR_WINDOW_STATE_NONE;
        if (libdecor_configuration_get_window_state(configuration, &state))
        {
            suspended = (static_cast<unsigned>(state) & LIBDECOR_WINDOW_STATE_SUSPENDED) != 0;
        }
        if (!libdecor_configuration_get_content_size(configuration, frame, &width, &height))
        {
            width = 0;
            height = 0;
        }
        window.Configuration.Configure(width, height, suspended);
        const auto size = window.Configuration.ContentSize();
        const std::unique_ptr<libdecor_state, WaylandDeleter<libdecor_state_free>> content{
            libdecor_state_new(static_cast<int>(size.Width), static_cast<int>(size.Height))};
        if (!content)
        {
            window.ConfigurationFailed = true;
            return;
        }
        libdecor_frame_commit(frame, content.get(), configuration);
        window.Configuration.Acknowledge();
    }

    [[nodiscard]] Status Create() noexcept
    {
        Connection.reset(wl_display_connect(nullptr));
        if (!Connection)
        {
            return WindowError();
        }
        const std::unique_ptr<wl_registry, WaylandDeleter<wl_registry_destroy>> registry{
            wl_display_get_registry(Connection.get())};
        static constexpr wl_registry_listener kGlobals{
            .global = BindGlobal,
            .global_remove = [](void*, wl_registry*, std::uint32_t) {},
        };
        if (!registry || wl_registry_add_listener(registry.get(), &kGlobals, this) < 0 ||
            wl_display_roundtrip(Connection.get()) < 0 || !Compositor || !HasShell)
        {
            return WindowError();
        }
        Surface.reset(wl_compositor_create_surface(Compositor.get()));
        if (!Surface)
        {
            return WindowError();
        }
        static libdecor_interface decorationEvents = []
        {
            libdecor_interface events{};
            events.error = [](libdecor*, libdecor_error, const char* message)
            { std::println(stderr, "Pangu Application: libdecor: {}", message); };
            return events;
        }();
        Decorations.reset(libdecor_new(Connection.get(), &decorationEvents));
        if (!Decorations)
        {
            return WindowError();
        }
        static libdecor_frame_interface frameEvents = []
        {
            libdecor_frame_interface events{};
            events.configure = Configure;
            events.close = [](libdecor_frame*, void* data) { static_cast<WaylandWindow*>(data)->Closing = true; };
            events.commit = [](libdecor_frame*, void* data)
            { wl_surface_commit(static_cast<WaylandWindow*>(data)->Surface.get()); };
            return events;
        }();
        Frame.reset(libdecor_decorate(Decorations.get(), Surface.get(), &frameEvents, this));
        if (!Frame)
        {
            return WindowError();
        }
        libdecor_frame_set_title(Frame.get(), "Pangu Application");
        libdecor_frame_set_app_id(Frame.get(), "PanguApplication");
        libdecor_frame_map(Frame.get());
        return {};
    }

    std::unique_ptr<wl_display, WaylandDeleter<wl_display_disconnect>> Connection;
    std::unique_ptr<wl_compositor, WaylandDeleter<wl_compositor_destroy>> Compositor;
    std::unique_ptr<wl_surface, WaylandDeleter<wl_surface_destroy>> Surface;
    std::unique_ptr<libdecor, WaylandDeleter<libdecor_unref>> Decorations;
    std::unique_ptr<libdecor_frame, WaylandDeleter<libdecor_frame_unref>> Frame;
    WaylandConfiguration Configuration;
    bool HasShell{};
    bool Closing{};
    bool ConfigurationFailed{};
};
} // namespace
} // namespace pg::app::detail

int main()
{
    pg::app::detail::WaylandWindow window;
    return pg::app::detail::GraphicsExitCode(pg::app::detail::RunGraphicsApplication(window));
}
