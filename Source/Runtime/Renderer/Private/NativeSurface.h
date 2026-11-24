#pragma once

#include <variant>

// 平台 SDK 的不透明类型仅用于借用，不将平台头传入跨平台状态机。
struct HWND__; // NOLINT(bugprone-reserved-identifier, readability-identifier-naming)
struct wl_display;
struct wl_surface;
struct ANativeWindow;

namespace pg::renderer
{
struct Win32Surface
{
    HWND__* Window{};
};

struct WaylandSurface
{
    wl_display* Display{};
    wl_surface* Surface{};
};

struct AndroidSurface
{
    ANativeWindow* Window{};
};

struct MetalSurface
{
    // 跨 C++ / Objective-C++ 边界借用 View 与 CAMetalLayer，由平台适配器保活。
    void* View{};
    void* Layer{};
};

using NativeSurfaceHandle = std::variant<Win32Surface, WaylandSurface, AndroidSurface, MetalSurface>;

// 仅 Engine 平台适配器构造；复制描述不会延长原生窗口或 View 的寿命。
class NativeSurface final
{
public:
    explicit NativeSurface(NativeSurfaceHandle handle) noexcept
        : HandleValue(handle)
    {
    }

    [[nodiscard]] const NativeSurfaceHandle& Handle() const noexcept { return HandleValue; }

private:
    NativeSurfaceHandle HandleValue;
};
} // namespace pg::renderer
