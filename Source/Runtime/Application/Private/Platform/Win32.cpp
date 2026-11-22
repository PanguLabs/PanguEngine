#include "../GraphicsApplication.h"

#include "NativeSurface.h"

#include <windows.h> // IWYU pragma: keep

#include <Error.h>
#include <Result.h>
#include <errhandlingapi.h>
#include <libloaderapi.h>
#include <minwindef.h>
#include <windef.h>
#include <winerror.h>
#include <winuser.h>

#include <cstdint>
#include <expected>
#include <optional>

namespace pg::app::detail
{
namespace
{
constexpr const wchar_t* kWindowClass = L"PanguApplication";
constexpr const wchar_t* kWindowTitle = L"Pangu Application";
constexpr int kInitialWidth = 720;
constexpr int kInitialHeight = 480;

[[nodiscard]] std::unexpected<Error> WindowError() noexcept
{
    return std::unexpected(
        Error{ErrorDomainId::Constant<0x70672e617070ULL>(), 1,
              NativeErrorCode{.Source = NativeErrorSourceId{0x77696e3332ULL}, .Value = GetLastError()}});
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_CLOSE)
    {
        // 保持 HWND 有效，直到 Renderer 确认当前 Surface 已退休。
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

class Win32Window final : public DesktopWindow
{
public:
    Win32Window() noexcept = default;
    Win32Window(const Win32Window&) = delete;
    Win32Window& operator=(const Win32Window&) = delete;
    Win32Window(Win32Window&&) = delete;
    Win32Window& operator=(Win32Window&&) = delete;

    ~Win32Window() override
    {
        if (Window != nullptr)
        {
            DestroyWindow(Window);
        }
        if (Class != 0)
        {
            UnregisterClassW(kWindowClass, Instance);
        }
    }

    [[nodiscard]] Status Create() noexcept
    {
        if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == FALSE &&
            GetLastError() != ERROR_ACCESS_DENIED)
        {
            return WindowError();
        }
        Instance = GetModuleHandleW(nullptr);
        if (Instance == nullptr)
        {
            return WindowError();
        }
        WNDCLASSW description{};
        description.lpfnWndProc = WindowProc;
        description.hInstance = Instance;
        description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        description.lpszClassName = kWindowClass;
        Class = RegisterClassW(&description);
        if (Class == 0)
        {
            return WindowError();
        }
        Window = CreateWindowW(kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                               kInitialWidth, kInitialHeight, nullptr, nullptr, Instance, nullptr);
        if (Window == nullptr)
        {
            return WindowError();
        }
        ShowWindow(Window, SW_SHOWNORMAL);
        return {};
    }

    [[nodiscard]] Result<std::optional<Drawable>> Poll() noexcept override
    {
        MSG event{};
        while (PeekMessageW(&event, nullptr, 0, 0, PM_REMOVE) != FALSE)
        {
            if (event.message == WM_QUIT)
            {
                return std::nullopt;
            }
            TranslateMessage(&event);
            DispatchMessageW(&event);
        }
        const renderer::NativeSurface surface{renderer::Win32Surface{.Window = Window}};
        if (IsIconic(Window) != FALSE)
        {
            return Drawable{.Surface = surface, .Size = {}};
        }
        RECT client{};
        if (GetClientRect(Window, &client) == FALSE)
        {
            return WindowError();
        }
        return Drawable{
            .Surface = surface,
            .Size =
                {
                    .Width = static_cast<std::uint32_t>(client.right - client.left),
                    .Height = static_cast<std::uint32_t>(client.bottom - client.top),
                },
        };
    }

    [[nodiscard]] Status WaitForEvent() noexcept override { return WaitMessage() != FALSE ? Status{} : WindowError(); }

private:
    HINSTANCE Instance{};
    HWND Window{};
    ATOM Class{};
};
} // namespace
} // namespace pg::app::detail

int main()
{
    pg::app::detail::Win32Window window;
    const auto created = window.Create();
    return pg::app::detail::GraphicsExitCode(created ? pg::app::detail::RunGraphicsApplication(window) : created);
}
