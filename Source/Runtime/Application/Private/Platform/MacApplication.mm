#include "../GraphicsApplication.h"

#include "NativeSurface.h"

#include <Error.h>
#include <RendererTypes.h>
#include <Result.h>

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>

#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>

@interface PanguWindowDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(nonatomic, readonly) BOOL closeRequested;
@end

@implementation PanguWindowDelegate
@synthesize closeRequested = CloseRequested;

- (BOOL)windowShouldClose:(NSWindow*)sender
{
    (void)sender;
    // 会话确认退休后才允许窗口和 layer 释放。
    CloseRequested = YES;
    return NO;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender
{
    (void)sender;
    CloseRequested = YES;
    return NSTerminateCancel;
}
@end

namespace pg::app::detail
{
namespace
{
constexpr CGFloat kInitialWidth = 720;
constexpr CGFloat kInitialHeight = 480;
constexpr NSTimeInterval kEventWaitSeconds = 0.001;

class MacWindow final : public DesktopWindow
{
public:
    MacWindow() noexcept = default;
    MacWindow(const MacWindow&) = delete;
    MacWindow& operator=(const MacWindow&) = delete;
    MacWindow(MacWindow&&) = delete;
    MacWindow& operator=(MacWindow&&) = delete;
    ~MacWindow() override
    {
        Window.delegate = nil;
        NSApp.delegate = nil;
        NSApp.mainMenu = nil;
        [Window close];
    }

    [[nodiscard]] Status Create() noexcept
    {
        NSApplication* const application = [NSApplication sharedApplication];
        Delegate = [PanguWindowDelegate new];
        application.delegate = Delegate;
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        [application finishLaunching];
        constexpr auto kStyle = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
                                NSWindowStyleMaskResizable;
        Window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, kInitialWidth, kInitialHeight)
                                             styleMask:kStyle
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
        if (Window == nil)
        {
            return std::unexpected(Error{ErrorDomainId::Constant<0x70672e617070ULL>(), 1});
        }
        Window.releasedWhenClosed = NO;
        Window.title = NSLocalizedString(@"Pangu Application", @"默认引擎窗口标题");
        Window.delegate = Delegate;

        View = [[NSView alloc] initWithFrame:Window.contentView.bounds];
        Layer = [CAMetalLayer layer];
        Layer.frame = View.bounds;
        View.layer = Layer;
        View.wantsLayer = YES;
        Window.contentView = View;

        NSMenu* const mainMenu = [NSMenu new];
        NSMenuItem* const appItem = [NSMenuItem new];
        [mainMenu addItem:appItem];
        appItem.submenu = [NSMenu new];
        NSMenuItem* const quit = [appItem.submenu addItemWithTitle:NSLocalizedString(@"Quit", @"退出应用菜单项")
                                                            action:@selector(performClose:)
                                                     keyEquivalent:@"q"];
        quit.target = Window;
        application.mainMenu = mainMenu;

        [Window center];
        [Window makeKeyAndOrderFront:nil];
        [application activate];
        return {};
    }

    [[nodiscard]] Result<std::optional<Drawable>> Poll() noexcept override
    {
        @autoreleasepool
        {
            if (Window == nil)
            {
                if (const auto created = Create(); !created)
                {
                    return std::unexpected(created.error());
                }
            }
            // 等待事件时也让 AppKit 提交窗口与 layer 的变更。
            DispatchEvent([NSDate dateWithTimeIntervalSinceNow:kEventWaitSeconds]);
            while (Delegate.closeRequested == NO && DispatchEvent(NSDate.distantPast))
            {
            }
            if (Delegate.closeRequested == YES)
            {
                return std::nullopt;
            }
            renderer::PixelSize size{};
            if (Window.visible == YES && Window.miniaturized == NO)
            {
                const NSRect backing = [View convertRectToBacking:View.bounds];
                size = {
                    .Width = static_cast<std::uint32_t>(std::lround(backing.size.width)),
                    .Height = static_cast<std::uint32_t>(std::lround(backing.size.height)),
                };
                const NSRect frame = View.bounds;
                const CGFloat scale = Window.backingScaleFactor;
                if (NSEqualRects(Layer.frame, frame) == NO || Layer.contentsScale != scale)
                {
                    [CATransaction begin];
                    [CATransaction setDisableActions:YES];
                    Layer.frame = frame;
                    Layer.contentsScale = scale;
                    [CATransaction commit];
                }
            }
            const renderer::MetalSurface surface{
                .View = (__bridge void*)View,
                .Layer = (__bridge void*)Layer,
            };
            return Drawable{.Surface = renderer::NativeSurface{surface}, .Size = size};
        }
    }

    [[nodiscard]] Status WaitForEvent() noexcept override
    {
        @autoreleasepool
        {
            DispatchEvent(NSDate.distantFuture);
            return {};
        }
    }

private:
    static bool DispatchEvent(NSDate* deadline) noexcept
    {
        NSEvent* const event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                  untilDate:deadline
                                                     inMode:NSDefaultRunLoopMode
                                                    dequeue:YES];
        if (event == nil)
        {
            return false;
        }
        [NSApp sendEvent:event];
        [NSApp updateWindows];
        return true;
    }

    NSWindow* Window = nil;
    NSView* View = nil;
    CAMetalLayer* Layer = nil;
    PanguWindowDelegate* Delegate = nil;
};
} // namespace
} // namespace pg::app::detail

int main()
{
    @autoreleasepool
    {
        pg::app::detail::MacWindow window;
        return pg::app::detail::GraphicsExitCode(pg::app::detail::RunGraphicsApplication(window));
    }
}
