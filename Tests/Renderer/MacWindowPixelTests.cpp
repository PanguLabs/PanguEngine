#include "Acceptance/MacWindowPixel.h"

#include <CoreGraphics/CGBitmapContext.h>
#include <CoreGraphics/CGColorSpace.h>
#include <CoreGraphics/CGContext.h>
#include <CoreGraphics/CGImage.h>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace pg::test
{
TEST(MacWindowPixel, ReadsCenterInSrgb)
{
    constexpr std::array<std::array<std::uint8_t, 4>, 2> kColors{{{63, 153, 200, 255}, {255, 0, 0, 255}}};
    for (const auto& color : kColors)
    {
        constexpr std::size_t kSide = 3;
        std::array<std::array<std::uint8_t, 4>, kSide * kSide> pixels{};
        pixels.at(pixels.size() / 2) = color;
        const std::unique_ptr<CGColorSpace, decltype(&CGColorSpaceRelease)> space(
            CGColorSpaceCreateWithName(kCGColorSpaceSRGB), CGColorSpaceRelease);
        ASSERT_NE(space, nullptr);
        const std::unique_ptr<CGContext, decltype(&CGContextRelease)> context(
            CGBitmapContextCreate(pixels.data(), kSide, kSide, 8, kSide * 4, space.get(),
                                  static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big),
            CGContextRelease);
        ASSERT_NE(context, nullptr);
        const std::unique_ptr<CGImage, decltype(&CGImageRelease)> image(CGBitmapContextCreateImage(context.get()),
                                                                        CGImageRelease);
        ASSERT_NE(image, nullptr);
        EXPECT_EQ(ReadSrgbCenter(image.get()), color);
    }
}
} // namespace pg::test
