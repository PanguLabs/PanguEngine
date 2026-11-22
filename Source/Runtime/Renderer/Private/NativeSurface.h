#pragma once

#include <variant>

// 保留 SDK 的不透明类型名称，避免把 Win32 和 Xlib 头传递到跨平台状态机。
struct HWND__;    // NOLINT(bugprone-reserved-identifier, readability-identifier-naming)
struct _XDisplay; // NOLINT(bugprone-reserved-identifier)
struct ANativeWindow;

namespace pg::renderer
{
struct Win32Surface
{
    HWND__* Window{};
};

struct XlibSurface
{
    _XDisplay* Display{};
    unsigned long Window{};
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

using NativeSurfaceHandle = std::variant<Win32Surface, XlibSurface, AndroidSurface, MetalSurface>;

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
