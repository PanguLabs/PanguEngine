#include "../GraphicsApplication.h"

#include "NativeSurface.h"

#include <Error.h>
#include <Result.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <unistd.h>

#include <bit>
#include <cstdint>
#include <expected>
#include <optional>

// Xlib 的宏会覆盖 Foundation 的状态类型。
#undef Status

namespace pg::app::detail
{
namespace
{
constexpr unsigned kInitialWidth = 720;
constexpr unsigned kInitialHeight = 480;
constexpr auto* kWindowTitle = "Pangu Application";

[[nodiscard]] std::unexpected<Error> WindowError() noexcept
{
    return std::unexpected(Error{ErrorDomainId::Constant<0x70672e617070ULL>(), 1});
}

class XlibWindow final : public DesktopWindow
{
public:
    ~XlibWindow() override
    {
        if (Connection != nullptr)
        {
            if (WindowId != 0)
            {
                XDestroyWindow(Connection, WindowId);
            }
            XCloseDisplay(Connection);
        }
    }

    [[nodiscard]] Result<std::optional<Drawable>> Poll() noexcept override
    {
        if (Connection == nullptr)
        {
            if (const auto created = Create(); !created)
            {
                return std::unexpected(created.error());
            }
        }
        while (XPending(Connection) > 0)
        {
            XEvent event{};
            XNextEvent(Connection, &event);
            switch (event.type)
            {
            case ClientMessage:
                if (event.xclient.window == WindowId && event.xclient.message_type == Protocols &&
                    event.xclient.format == 32 && static_cast<Atom>(event.xclient.data.l[0]) == DeleteWindow)
                {
                    // 由 GraphicsApplication 确认 Vulkan 链已退休后再销毁 X11 窗口。
                    return std::nullopt;
                }
                break;
            case ConfigureNotify:
                Size = {.Width = static_cast<std::uint32_t>(event.xconfigure.width),
                        .Height = static_cast<std::uint32_t>(event.xconfigure.height)};
                break;
            case MapNotify:
                Mapped = true;
                break;
            case UnmapNotify:
                Mapped = false;
                break;
            case DestroyNotify:
                WindowId = 0;
                return std::nullopt;
            default:
                break;
            }
        }
        return Drawable{
            .Surface = renderer::NativeSurface{renderer::XlibSurface{.Display = Connection, .Window = WindowId}},
            .Size = Mapped ? Size : renderer::PixelSize{},
        };
    }

    [[nodiscard]] Status WaitForEvent() noexcept override
    {
        XEvent event{};
        XPeekEvent(Connection, &event);
        return {};
    }

private:
    [[nodiscard]] Status Create() noexcept
    {
        Connection = XOpenDisplay(nullptr);
        if (Connection == nullptr)
        {
            return WindowError();
        }
        WindowId = XCreateSimpleWindow(Connection, XDefaultRootWindow(Connection), 0, 0, kInitialWidth, kInitialHeight,
                                       0, 0, 0);
        if (WindowId == 0)
        {
            return WindowError();
        }
        Protocols = XInternAtom(Connection, "WM_PROTOCOLS", False);
        DeleteWindow = XInternAtom(Connection, "WM_DELETE_WINDOW", False);
        if (Protocols == None || DeleteWindow == None || XSetWMProtocols(Connection, WindowId, &DeleteWindow, 1) == 0)
        {
            return WindowError();
        }
        XStoreName(Connection, WindowId, kWindowTitle);
        const auto process = static_cast<unsigned long>(getpid());
        const Atom processProperty = XInternAtom(Connection, "_NET_WM_PID", False);
        constexpr int kCardinalBits = 32;
        XChangeProperty(Connection, WindowId, processProperty, XA_CARDINAL, kCardinalBits, PropModeReplace,
                        std::bit_cast<const unsigned char*>(&process), 1);
        XSelectInput(Connection, WindowId, StructureNotifyMask);
        XMapWindow(Connection, WindowId);
        XFlush(Connection);
        return {};
    }

    Display* Connection{};
    Window WindowId{};
    Atom Protocols{};
    Atom DeleteWindow{};
    renderer::PixelSize Size{.Width = kInitialWidth, .Height = kInitialHeight};
    bool Mapped{};
};
} // namespace
} // namespace pg::app::detail

int main()
{
    // Forge 可能调用 Xlib，须先初始化 Xlib 的线程支持。
    if (XInitThreads() == 0)
    {
        return pg::app::detail::GraphicsExitCode(pg::app::detail::WindowError());
    }
    pg::app::detail::XlibWindow window;
    return pg::app::detail::GraphicsExitCode(pg::app::detail::RunGraphicsApplication(window));
}
