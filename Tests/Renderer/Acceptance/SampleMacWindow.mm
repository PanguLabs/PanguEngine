#include "MacWindowPixel.h"

#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <cstdio>
#include <cstdlib>
#include <print>

namespace
{
[[noreturn]] void Finish(int status) noexcept
{
    // 回调和超时共用单次采样进程，刷新输出后直接结束。
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(status);
}

int PrintCenterColor(CGImageRef image)
{
    const auto pixel = pg::test::ReadSrgbCenter(image);
    if (!pixel)
    {
        std::fputs("Could not read the window center in sRGB.\n", stderr);
        return EXIT_FAILURE;
    }
    std::println("#{:02X}{:02X}{:02X}", pixel->at(0), pixel->at(1), pixel->at(2));
    return EXIT_SUCCESS;
}

void SampleWindow(pid_t pid, SCShareableContent* content, NSError* error) API_AVAILABLE(macos(14.0))
{
    if (error != nil)
    {
        std::println(stderr, "Window enumeration failed: {}", error.localizedDescription.UTF8String);
        Finish(EXIT_FAILURE);
    }
    for (SCWindow* window in content.windows)
    {
        if (window.owningApplication.processID != pid || window.windowLayer != 0 ||
            [window.title isEqualToString:@"Pangu Application"] == NO)
        {
            continue;
        }
        SCContentFilter* const filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
        SCStreamConfiguration* const config = [SCStreamConfiguration new];
        config.showsCursor = NO;
        config.ignoreShadowsSingleWindow = YES;
        config.colorSpaceName = kCGColorSpaceSRGB;
        const auto captured = ^(CGImageRef image, NSError* captureError) {
          if (image == nullptr || captureError != nil)
          {
              std::fputs("Window capture failed.\n", stderr);
              Finish(EXIT_FAILURE);
          }
          Finish(PrintCenterColor(image));
        };
        [SCScreenshotManager captureImageWithFilter:filter configuration:config completionHandler:captured];
        return;
    }
    // 窗口映射可能晚于首批呈现调用，交由观察器在期限内重试。
    std::puts("window-pending");
    Finish(EXIT_SUCCESS);
}
} // namespace

int main()
{
    @autoreleasepool
    {
        NSArray<NSString*>* const arguments = NSProcessInfo.processInfo.arguments;
        if (arguments.count != 2 || arguments.lastObject.intValue <= 0)
        {
            return EXIT_FAILURE;
        }
        const pid_t pid = arguments.lastObject.intValue;
        if (!CGPreflightScreenCaptureAccess())
        {
            std::fputs("Pixel sampling requires Screen Recording permission.\n", stderr);
            return EXIT_FAILURE;
        }
        if (@available(macOS 14.0, *))
        {
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
              std::fputs("Window capture timed out.\n", stderr);
              Finish(EXIT_FAILURE);
            });
            const auto enumerated = ^(SCShareableContent* content, NSError* error) {
              SampleWindow(pid, content, error);
            };
            [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                                       onScreenWindowsOnly:YES
                                                         completionHandler:enumerated];
            dispatch_main();
        }
        std::fputs("Window sampling requires macOS 14 or later.\n", stderr);
        return EXIT_FAILURE;
    }
}
